/**
 * @file pet_assets.cpp
 * @brief Implementation of PetAssets::load: the package checks, the data files, and the size of every sprite sheet.
 */

#include "app/pet/pet_assets.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <QChar>
#include <QImage>
#include <QImageReader>
#include <QString>

#include "app/paths.h"
#include "app/pet/pet_package_fs.h"
#include "core/domain/pet/pet_package.h"
#include "util/log.h"

namespace lens::app::pet {

namespace {

using nlohmann::json;
using core::pet::Refusal;
using core::pet::Refused;

/// Alpha at or above this counts as drawn, so faint anti-aliased edges do not pull the centre sideways.
constexpr int kInkAlpha = 40;

/// @return The document in `file`, parsed. A missing file is Unreadable; text that is not JSON is Format.
json readDocument(std::filesystem::path const& file)
{
    auto stream = std::ifstream{file};
    if (not stream) throw Refused(Refusal::Unreadable, "pet: cannot open " + narrow(file));
    try {
        return json::parse(stream);
    } catch (json::parse_error const& e) {
        throw Refused(Refusal::Format, "pet: " + narrow(file) + " is not valid JSON: " + e.what());
    }
}

/// @return `entry[key]` as a string. A missing or non-string value is refused as `invalid`, naming `owner`.
std::string stringOf(json const& entry, char const* key, Refusal invalid, std::string const& owner)
{
    if (not entry.contains(key) or not entry.at(key).is_string()) throw Refused(invalid, owner + "." + key + " must be a string");
    return entry.at(key).get<std::string>();
}

/**
 * @brief The name the settings show: the package's own name with control and format characters removed, in NFC.
 *
 * A name that is empty once cleaned is refused, and so is one past the length limit once cleaned. A package with no name
 * shows its id.
 *
 * @param written The `name` as the package wrote it, if it wrote one.
 * @param id The package's id, which stands in for a missing name.
 * @throws Refused BadName when nothing is left, TooLarge when it is over 32 characters.
 */
QString displayNameOf(std::optional<std::string> const& written, std::string const& id)
{
    if (not written) return QString::fromStdString(id);
    auto const kept = plainText(QString::fromStdString(*written));
    if (kept.isEmpty()) throw Refused(Refusal::BadName, "pet: the name is empty once control characters are removed");
    if (kept.toUcs4().size() > static_cast<std::size_t>(core::pet::kMaxNameChars))
        throw Refused(Refusal::TooLarge, "pet: the name is over " + std::to_string(core::pet::kMaxNameChars) + " characters");
    return kept;
}

/**
 * @brief The declared size of a PNG, read from its header. Nothing is decoded.
 *
 * This is the check before a decoder would allocate: a file with a tiny body can declare a huge image, so the declared
 * size is held to the sheet limits first.
 *
 * @param file A file already resolved inside its package.
 * @throws Refused Unreadable if it cannot be opened, BadAssetType if it is not a PNG or is not named .png, TooLarge past
 *         the sheet limits.
 */
core::pet::PngSize declaredSizeOf(std::filesystem::path const& file)
{
    // The type comes from the magic, and the name must agree with it (docs/pet-package-format.md, section 6).
    if (widen(file.extension()).compare(QStringLiteral(".png"), Qt::CaseInsensitive) != 0)
        throw Refused(Refusal::BadAssetType, "pet: not a .png file: " + narrow(file));
    auto stream = std::ifstream{file, std::ios::binary};
    if (not stream) throw Refused(Refusal::Unreadable, "pet: cannot open " + narrow(file));
    auto head = std::array<char, 24>{};
    stream.read(head.data(), static_cast<std::streamsize>(head.size()));
    auto const read = std::string_view{head.data(), static_cast<std::size_t>(stream.gcount())};

    auto const declared = core::pet::readPngSize(read);
    if (not declared) throw Refused(Refusal::BadAssetType, "pet: not a PNG: " + narrow(file));
    auto const pixels = static_cast<std::uint64_t>(declared->width) * declared->height;
    if (declared->width > static_cast<std::uint32_t>(core::pet::kMaxSheetSide) or
        declared->height > static_cast<std::uint32_t>(core::pet::kMaxSheetSide) or pixels > core::pet::kMaxPixels)
        throw Refused(Refusal::TooLarge, "pet: an image declares more pixels than a sheet may have: " + narrow(file));
    return *declared;
}

/**
 * @brief Reads a PNG's declared size and charges its pixels against the package's decoded-pixel budget.
 * @param file A file already resolved inside its package.
 * @param pixels The running total for the package, which this adds to.
 * @return The declared size.
 * @throws Refused as declaredSizeOf does, or TooLarge when the package as a whole decodes to more than 16 M pixels.
 */
core::pet::PngSize chargedSizeOf(std::filesystem::path const& file, std::uint64_t& pixels)
{
    auto const size = declaredSizeOf(file);
    pixels += static_cast<std::uint64_t>(size.width) * size.height;
    if (pixels > core::pet::kMaxPixels) throw Refused(Refusal::TooLarge, "pet: the package decodes to more than 16 M pixels");
    return size;
}

/// @brief Refuses an image whose declared size is not `width` by `height`. Sheets are laid out in one row, so it is exact.
void requireSize(core::pet::PngSize size, int width, int height, std::string const& name)
{
    if (static_cast<std::int64_t>(size.width) != width or static_cast<std::int64_t>(size.height) != height) {
        throw Refused(Refusal::SheetSize, "pet: " + name + " is not " + std::to_string(width) + "x" + std::to_string(height));
    }
}

/**
 * @return The expression index of each frame, or nothing when the action keeps expression 0.
 * @throws Refused ExpressionIndex for an index that is not a whole number or names no cell of the expression sheet.
 */
std::vector<int> expressionsOf(json const& entry, int cells, std::string const& owner)
{
    if (not entry.contains("expressions")) return {};
    if (not entry.at("expressions").is_array()) throw Refused(Refusal::ExpressionIndex, owner + ".expressions must be a list");
    auto indices = std::vector<int>{};
    for (auto const& cell : entry.at("expressions")) {
        if (not cell.is_number_integer()) throw Refused(Refusal::ExpressionIndex, owner + " has an index that is not a whole number");
        auto const index = cell.get<std::int64_t>();
        if (index < 0 or index >= cells)
            throw Refused(Refusal::ExpressionIndex, owner + " names expression " + std::to_string(index) + " of " + std::to_string(cells));
        indices.push_back(static_cast<int>(index));
    }
    return indices;
}

/// @brief Where the figure drawn in the first frame of a sheet sits: its ink box, horizontal centre and feet.
struct InkBounds {
    double left;
    double top;
    double right;
    double bottom;
    double centre;
};

/// @return The ink box, centre and feet of what the sheet at `path` draws in its first frame, in sheet units. The
///         whole canvas when it draws nothing.
InkBounds inkBoundsOf(QString const& path, int canvas)
{
    auto const sheet  = QImageReader{path}.read().convertToFormat(QImage::Format_ARGB32);
    auto       left   = canvas;
    auto       top    = canvas;
    auto       right  = 0;
    auto       bottom = 0;
    for (auto y = 0; y < canvas; ++y) {
        for (auto x = 0; x < canvas; ++x) {
            if (qAlpha(sheet.pixel(x, y)) < kInkAlpha) continue;
            left   = std::min(left, x);
            top    = std::min(top, y);
            right  = std::max(right, x + 1);
            bottom = std::max(bottom, y + 1);
        }
    }
    if (right == 0) return InkBounds{0.0, 0.0, static_cast<double>(canvas), static_cast<double>(canvas), canvas / 2.0};
    return InkBounds{
        .left   = static_cast<double>(left),
        .top    = static_cast<double>(top),
        .right  = static_cast<double>(right),
        .bottom = static_cast<double>(bottom),
        .centre = (left + right) / 2.0,
    };
}

/**
 * @brief Reads a bundled pet's review sign: sign.json, and every sheet its bins name at the size the bin says.
 *
 * A folder with no sign.json has no sign and says nothing. Any other fault drops the sign with one warning; the pet loads
 * without it.
 *
 * @param directory One bundled pet's folder.
 * @return The sign, or nothing.
 */
std::optional<ReviewSign> reviewSignOf(std::filesystem::path const& directory)
{
    try {
        if (not std::filesystem::exists(directory / "sign.json")) return std::nullopt;
        auto const  document = readDocument(directory / "sign.json");
        auto const& word     = document.at("word");
        auto        sign     = ReviewSign{
                       .frames    = document.at("frames").get<int>(),
                       .every     = document.at("every").get<int>(),
                       .beatMs    = document.at("beatMs").get<int>(),
                       .margin    = document.at("margin").get<int>(),
                       .foot      = document.at("foot").get<int>(),
                       .wordTop   = word.at("top").get<int>(),
                       .wordColor = QString::fromStdString(word.at("color").get<std::string>()),
        };
        if (sign.frames < 1 or sign.every < 1 or sign.beatMs < 1 or sign.margin < 0)
            throw Refused(Refusal::Format, "pet: the review sign has a frame count, beat or margin out of range");
        for (auto const& entry : document.at("bins")) {
            auto const width      = entry.at("width").get<int>();
            auto const cellWidth  = entry.at("cellWidth").get<int>();
            auto const cellHeight = entry.at("cellHeight").get<int>();
            if (not sign.bins.empty() and width <= sign.bins.back().width)
                throw Refused(Refusal::Format, "pet: the review sign's bins are not in ascending order of width");
            auto const sheet = resolvePackageFile(directory, stringOf(entry, "sheet", Refusal::Format, "sign bin"));
            auto const size  = declaredSizeOf(sheet);
            if (static_cast<std::int64_t>(size.width) != static_cast<std::int64_t>(cellWidth) * sign.frames or
                static_cast<std::int64_t>(size.height) != cellHeight)
                throw Refused(Refusal::SheetSize, "pet: " + narrow(sheet) + " is not the size its review sign bin says");
            sign.bins.push_back(ReviewSignBin{
                .width      = width,
                .boardWidth = entry.at("W").get<int>(),
                .cellWidth  = cellWidth,
                .cellHeight = cellHeight,
                .sheet      = widen(sheet),
            });
        }
        if (sign.bins.empty()) throw Refused(Refusal::Format, "pet: the review sign has no bins");
        return sign;
    } catch (std::exception const& e) {
        LENS_WARN("the review sign of {} is dropped: {}", narrow(directory.filename()), e.what());
        return std::nullopt;
    }
}

} // namespace

QString plainText(QString const& text)
{
    auto kept = QString{};
    for (auto const point : text.toUcs4()) {
        auto const category = QChar::category(point);
        if (category == QChar::Other_Control or category == QChar::Other_Format) continue;
        kept += QString::fromUcs4(&point, 1);
    }
    return kept.normalized(QString::NormalizationForm_C);
}

PetAssets PetAssets::load(std::filesystem::path const& directory, PetSource source)
{
    try {
        checkPackageTree(directory);

        auto const document = core::pet::parsePetDocument(readDocument(directory / "pet.json"));
        if (narrow(directory.filename()) != document.id)
            throw Refused(Refusal::BadName, "pet: id " + document.id + " is not the name of its folder");

        auto assets   = PetAssets{};
        assets.name   = displayNameOf(document.name, document.id);
        assets.canvas = document.canvas;
        assets.scale  = document.scale;
        assets.layers = Layers{document.bodyLayer, document.expressionLayer, document.effectLayer};

        auto pixels = std::uint64_t{0};

        // The expression sheet is one row of canvas-sized cells: its height is the canvas, its width a whole number of cells.
        auto const canvas         = static_cast<std::uint32_t>(assets.canvas);
        auto const expressionFile = resolvePackageFile(directory, document.expression);
        auto const expressionSize = chargedSizeOf(expressionFile, pixels);
        if (expressionSize.height != canvas or expressionSize.width % canvas != 0)
            throw Refused(Refusal::SheetSize, "pet: expression sheet is not a row of canvas-sized cells");
        auto const cells = static_cast<int>(expressionSize.width / canvas);
        if (cells > core::pet::kMaxExpressionCells)
            throw Refused(Refusal::TooLarge, "pet: more than " + std::to_string(core::pet::kMaxExpressionCells) + " expressions");
        assets.expressionSheet = widen(expressionFile);

        auto const animations = readDocument(directory / "animations.json");
        assets.animations     = core::pet::parseAnimations(animations);
        auto const& playbook  = assets.animations.playbook;
        for (auto const& [name, entry] : animations.at("actions").items()) {
            if (name == "blink") {
                assets.blinkExpressions = expressionsOf(entry, cells, "animations: blink");
                continue;
            }
            // Frames and canvas are both bounded by the parser, so this product cannot overflow.
            auto const row = playbook.actions.at(name).frames * assets.canvas;

            ActionSheets pictures;
            auto const   body = resolvePackageFile(directory, stringOf(entry, "sheet", Refusal::BadAnimation, "animations: " + name));
            requireSize(chargedSizeOf(body, pixels), row, assets.canvas, "the body sheet of " + name);
            pictures.body = widen(body);
            if (entry.contains("effect")) {
                auto const effect = resolvePackageFile(directory, stringOf(entry, "effect", Refusal::BadAnimation, "animations: " + name));
                requireSize(chargedSizeOf(effect, pixels), row, assets.canvas, "the effect sheet of " + name);
                pictures.effect = widen(effect);
            }
            pictures.expressions = expressionsOf(entry, cells, "animations: " + name);
            assets.sheets.emplace(name, std::move(pictures));
        }

        assets.anchors = core::pet::parseAnchors(readDocument(directory / "anchors.json"), playbook);

        // The idle action is the one the role names, not a fixed folder name: the data decides what the idle role plays.
        auto const& idle    = playbook.roles.at(core::pet::Role::Idle);
        auto const  bounds  = inkBoundsOf(assets.sheets.at(idle).body, assets.canvas);
        assets.inkLeft      = bounds.left;
        assets.inkTop       = bounds.top;
        assets.inkRight     = bounds.right;
        assets.inkCentre    = bounds.centre;
        assets.inkBottom    = bounds.bottom;
        assets.peekInkLeft  = bounds.left;
        assets.peekInkRight = bounds.right;
        if (auto const& movement = assets.animations.movement; movement and movement->peek) {
            auto const peek     = inkBoundsOf(assets.sheets.at(*movement->peek).body, assets.canvas);
            assets.peekInkLeft  = peek.left;
            assets.peekInkRight = peek.right;
        }

        auto const accessories = readDocument(directory / "accessories.json");
        assets.catalogue       = core::pet::parseAccessories(accessories, playbook);
        for (auto const& entry : accessories.at("accessories")) {
            auto const id    = entry.at("id").get<std::string>();
            auto const asset = resolvePackageFile(directory, stringOf(entry, "asset", Refusal::BadAccessory, "accessories: " + id));
            requireSize(chargedSizeOf(asset, pixels), assets.canvas, assets.canvas, "the accessory " + id);
            if (not entry.contains("zIndex") or not entry.at("zIndex").is_number_integer())
                throw Refused(Refusal::BadAccessory, "accessories: " + id + " needs an integer zIndex");
            assets.accessories.emplace(id, AccessoryArt{widen(asset), entry.at("zIndex").get<int>()});
        }
        if (source == PetSource::Shipped) assets.reviewSign = reviewSignOf(directory);
        return assets;
    } catch (json::exception const& e) {
        // A document the parsers did not expect in shape. Every field read above is checked first, so this is the net.
        throw Refused(Refusal::Format, std::string{"pet: unexpected document shape: "} + e.what());
    }
}

} // namespace lens::app::pet
