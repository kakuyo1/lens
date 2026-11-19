/** @file subtitle_track.cpp
 * @brief The clock loop, the cue cache and the caption layout behind subtitle_track.h.
 */

#include "app/platform/subtitle_track.h"

#include "app/platform/potplayer_clock.h"
#include "capture/word_track.h"
#include "core/subtitle/subtitle_source.h"
#include "util/log.h"
#include "util/text.h"

#include <QFont>
#include <QFontMetricsF>
#include <QImage>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <windows.h>

namespace lens::app {
namespace {

using core::subtitle::Anchor;
using core::subtitle::Cue;
using core::subtitle::CueTimeline;

// One clock query per poll; the status rides along to set the cadence.
constexpr std::chrono::milliseconds kPlayingPoll{50};
constexpr std::chrono::milliseconds kPausedPoll{150};
constexpr std::chrono::milliseconds kTitlePoll{1000};

// Geometry measured on the player's default caption style (Segoe UI, size 15, weight 700) on
// 2026-11-19 against a 1920x1080 video shown in a 1593x896 picture: a 46 px caption whose last
// baseline is 63 px above the picture's bottom edge, 67 px between a two-line cue's baselines
// (docs/adr/0201). The margins follow the picture, the line gap follows the font.
constexpr double kReferencePictureHeight = 896.0;
constexpr double kReferenceFontPixels    = 46.0;
constexpr double kReferenceLineGap       = 67.0;
constexpr double kReferenceBottomMargin  = 63.0;
constexpr double kReferenceTopMargin     = 40.0;
/// The word's box ends this far under the baseline, where the layer draws its dash: the glyphs'
/// feet, not the line box's descent.
constexpr int kUnderlineGap = 2;

struct CaptionStyle {
    QString family = QStringLiteral("Segoe UI");
    double  size   = 15.0;
    int     weight = 700;
};

/// @brief The player's caption style from its own settings, defaults kept where it cannot say.
CaptionStyle readCaptionStyle(std::wstring const& executablePath)
{
    CaptionStyle style;
    auto const   stem = std::filesystem::path{executablePath}.stem().wstring();
    if (stem.empty()) return style;
    std::wstring const key = L"Software\\DAUM\\" + stem + L"\\Settings";
    wchar_t            buffer[4096];
    DWORD              size = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"CaptionStyle", RRF_RT_REG_SZ, nullptr, buffer, &size) !=
        ERROR_SUCCESS)
        return style;
    auto const fields = QString::fromWCharArray(buffer).split(QLatin1Char(';'));
    if (fields.size() <= 24) return style;
    auto const family = fields[19].trimmed();
    bool       ok     = false;
    auto const points = fields[20].toDouble(&ok);
    auto const weight = fields[24].toInt();
    if (not family.isEmpty()) style.family = family;
    if (ok and points > 0 and points < 1000) style.size = points;
    if (weight >= 1 and weight <= 1000) style.weight = weight;
    return style;
}

struct Token {
    int         id = 0;
    std::string letters; ///< UTF-8, trailing punctuation cut -- the surface the scan judges.
    int         line = 0;
};

/// @brief Drops the last @p cut code points, walking code point boundaries from the end.
std::string dropTrailingCodePoints(std::string_view const text, int const cut)
{
    std::size_t end = text.size();
    for (int dropped = 0; dropped < cut and end > 0; ++dropped) {
        --end;
        while (end > 0 and (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
            --end;
    }
    return std::string{text.substr(0, end)};
}

/// @brief The cue's words: split on whitespace, each carrying its line, punctuation-only tokens
///        left out.
std::vector<Token> tokensOf(Cue const& cue, int* const nextId)
{
    std::vector<Token> tokens;
    for (int line = 0; line < static_cast<int>(cue.lines.size()); ++line) {
        auto const& text  = cue.lines[static_cast<std::size_t>(line)];
        std::size_t start = 0;
        while (start < text.size()) {
            while (start < text.size() and util::isAsciiSpace(text[start]))
                ++start;
            std::size_t end = start;
            while (end < text.size() and not util::isAsciiSpace(text[end]))
                ++end;
            if (end <= start) break;
            auto const  word    = std::string_view{text}.substr(start, end - start);
            auto const  cut     = capture::trailingPunctOf(word);
            std::string letters = cut > 0 ? dropTrailingCodePoints(word, cut) : std::string{word};
            if (not letters.empty()) tokens.push_back({(*nextId)++, std::move(letters), line});
            start = end;
        }
    }
    return tokens;
}

struct LaidWord {
    int id = 0;
    int x  = 0;
    int y  = 0;
    int w  = 0;
    int h  = 0;
};

/// @brief The cue's words as physical screen boxes: lines centered, the block at the bottom (or
///        top) margin the player draws its captions at.
std::vector<LaidWord> layOut(std::vector<Token> const& tokens, Cue const& cue, QRect const& picture, CaptionStyle const& style)
{
    std::vector<LaidWord> laid;
    if (tokens.empty() or picture.isEmpty()) return laid;
    auto const fontPx = std::clamp(
        qRound(kReferenceFontPixels * style.size / 15.0 * picture.height() / kReferencePictureHeight), 6, 400);
    QFont font{style.family};
    font.setPixelSize(fontPx);
    font.setWeight(static_cast<QFont::Weight>(std::clamp(style.weight, 1, 1000)));
    // A probe device pins the metrics to a 1:1 device, so the numbers are pixels regardless of
    // the screen's DPI; pixel sizes scale the advances linearly.
    QImage const        probe{1, 1, QImage::Format_ARGB32};
    QFontMetricsF const metrics{font, &probe};
    auto const          spaceWidth = metrics.horizontalAdvance(QLatin1Char(' '));
    auto const          ascent     = metrics.ascent();
    auto const          boxHeight  = qRound(ascent) + kUnderlineGap;

    auto const lineGap = kReferenceLineGap * fontPx / kReferenceFontPixels;
    auto const margin  = (cue.anchor == Anchor::Top ? kReferenceTopMargin : kReferenceBottomMargin) *
                        picture.height() / kReferencePictureHeight;
    auto const baselineOf = [&](int const line) {
        auto const lines = static_cast<int>(cue.lines.size());
        return cue.anchor == Anchor::Top ? picture.top() + margin + ascent + line * lineGap
                                         : picture.bottom() + 1 - margin - (lines - 1 - line) * lineGap;
    };

    for (int line = 0; line < static_cast<int>(cue.lines.size()); ++line) {
        double width = 0;
        bool   first = true;
        for (auto const& token : tokens) {
            if (token.line != line) continue;
            if (not first) width += spaceWidth;
            width += metrics.horizontalAdvance(QString::fromStdString(token.letters));
            first = false;
        }
        auto const y = qRound(baselineOf(line) - ascent);
        double     x = picture.center().x() + 0.5 - width / 2;
        for (auto const& token : tokens) {
            if (token.line != line) continue;
            auto const advance = metrics.horizontalAdvance(QString::fromStdString(token.letters));
            laid.push_back({token.id, qRound(x), y, qRound(advance), boxHeight});
            x += advance + spaceWidth;
        }
    }
    return laid;
}

struct CacheEntry {
    std::wstring                 key;
    core::subtitle::SubtitleLoad load;
};

std::mutex              g_cacheMutex;
std::vector<CacheEntry> g_cache; // most recent first, at most two

std::wstring cacheKeyOf(std::filesystem::path const& path)
{
    std::error_code error;
    auto const      size = std::filesystem::file_size(path, error);
    auto const      time = std::filesystem::last_write_time(path, error);
    return path.wstring() + L'|' + std::to_wstring(size) + L'|' + std::to_wstring(time.time_since_epoch().count());
}

/// @brief The media's subtitles, from the last two files read or freshly loaded; the 2 GB walk a
///        fresh Matroska costs is paid once per file, not per focus switch.
core::subtitle::SubtitleLoad loadCached(std::filesystem::path const& media)
{
    auto const            key = cacheKeyOf(media);
    std::lock_guard const lock{g_cacheMutex};
    if (auto const found = std::find_if(g_cache.begin(), g_cache.end(), [&key](CacheEntry const& entry) { return entry.key == key; });
        found != g_cache.end()) {
        if (found != g_cache.begin()) std::rotate(g_cache.begin(), found, found + 1);
        return g_cache.front().load;
    }
    auto load = core::subtitle::loadSubtitlesFor(media);
    g_cache.insert(g_cache.begin(), {key, load});
    if (g_cache.size() > 2) g_cache.pop_back();
    return load;
}

}

SubtitleTrack::SubtitleTrack(quintptr const window, QRect const band, Deliver deliver)
    : window_{window}, deliver_{std::move(deliver)}, band_{band}
{
    worker_ = std::jthread{[this](std::stop_token const& stop) { run(stop); }};
}

SubtitleTrack::~SubtitleTrack()
{
    worker_.request_stop();
}

void SubtitleTrack::setBand(QRect const band)
{
    std::lock_guard const lock{guard_};
    band_ = band;
}

void SubtitleTrack::run(std::stop_token const& stop)
{
    QString            title;
    std::vector<Cue>   cues;
    CueTimeline        timeline;
    std::vector<Token> tokens;
    CaptionStyle       style;
    Cue const*         lastCue = nullptr;
    QRect              lastBand;
    bool               emitPending  = false;
    bool               addedPending = false;
    bool               styleRead    = false;
    bool               playing      = false;
    int                nextId       = 0;
    auto               titleAt      = std::chrono::steady_clock::time_point{}; // due on the first pass

    while (not stop.stop_requested()) {
        QRect band;
        {
            std::lock_guard const lock{guard_};
            band = band_;
        }
        if (band != lastBand) {
            lastBand    = band;
            emitPending = true;
        }

        auto const now = std::chrono::steady_clock::now();
        if (now - titleAt >= kTitlePoll) {
            titleAt          = now;
            auto const fresh = potplayerWindowTitle(window_);
            if (fresh != title) {
                title       = fresh;
                emitPending = true;
                cues.clear();
                tokens.clear();
                lastCue = nullptr;
                nextId  = 0;
                if (auto const media = potplayerMediaPath(window_)) {
                    auto load = loadCached(*media);
                    cues      = std::move(load.cues);
                    if (not load.note.empty()) LENS_WARN("scan subtitles: {}", load.note);
                    if (not cues.empty()) {
                        auto const u8 = media->u8string();
                        LENS_INFO("scan subtitles: {} cue(s) from {} ({})", cues.size(), std::string{reinterpret_cast<char const*>(u8.data()), u8.size()}, load.codec);
                    }
                } else if (not title.isEmpty() and title.compare(QLatin1String("PotPlayer"), Qt::CaseInsensitive) != 0) {
                    LENS_WARN("scan subtitles: the player shows '{}' but its file could not be resolved",
                              title.toStdString());
                }
                timeline.reset(&cues);
            }
        }

        if (not cues.empty()) {
            if (not styleRead) {
                style     = readCaptionStyle(potplayerExecutablePath(window_));
                styleRead = true;
            }
            if (auto const timeMs = potplayerTimeMs(window_)) {
                auto const* const cue = timeline.at(*timeMs);
                if (cue != lastCue) {
                    lastCue = cue;
                    tokens.clear();
                    if (cue != nullptr) tokens = tokensOf(*cue, &nextId);
                    addedPending = not tokens.empty();
                    emitPending  = true;
                }
            }
        }
        playing = potplayerPlayStatus(window_).value_or(0) == 2;

        if (emitPending) {
            emitPending = false;
            capture::TrackFrame frame;
            if (auto const video = potplayerVideoRect(window_); video and lastCue != nullptr and not tokens.empty()) {
                for (auto const& word : layOut(tokens, *lastCue, *video, style)) {
                    int const cx = word.x + word.w / 2;
                    int const cy = word.y + word.h / 2;
                    if (cx < band.x() or cx >= band.x() + band.width() or cy < band.y() or cy >= band.y() + band.height())
                        continue;
                    frame.visible.push_back({word.id, word.x, word.y, word.w, word.h});
                }
                if (addedPending)
                    for (auto const& token : tokens)
                        frame.added.push_back({token.id, token.letters});
            }
            addedPending = false;
            deliver_(std::move(frame));
        }

        std::this_thread::sleep_for(playing ? kPlayingPoll : kPausedPoll);
    }
}

}
