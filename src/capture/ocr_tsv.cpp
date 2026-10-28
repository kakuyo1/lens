/** @file ocr_tsv.cpp
 * @brief Bounded, row-oriented Tesseract TSV parsing and line reconstruction.
 */
#include "ocr_tsv.h"

#include <QHash>
#include <QStringConverter>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace lens::capture {
namespace {

constexpr std::size_t kMaxLines           = 2000;
constexpr std::size_t kMaxWords           = 20000;
constexpr std::size_t kMaxRows            = 22000;
constexpr std::size_t kMaxTextUnits       = 65536;
constexpr std::size_t kMaxStructuredBytes = 4 * 1024 * 1024;
constexpr char const* kHeader             = "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext";

struct LineAccum {
    std::optional<OcrBox> box;
    std::vector<OcrWord>  words;
};

bool decodeStrict(QByteArray const& bytes, QString& out)
{
    QStringDecoder decoder{QStringDecoder::Utf8};
    out = decoder.decode(bytes);
    return not decoder.hasError() and not out.contains(QChar(0));
}

bool isCjk(QChar character)
{
    auto const code = character.unicode();
    return (code >= 0x2E80 and code <= 0x9FFF) or (code >= 0xF900 and code <= 0xFAFF) or
           (code >= 0xFF00 and code <= 0xFFEF);
}

/// @return Whether a space belonged between @p previous and @p word: the box gap says so when it
///         exists, and the script boundary is the fallback when the engine gave no boxes.
bool separated(OcrWord const& previous, OcrWord const& word)
{
    if (previous.box and word.box) {
        auto const gap    = word.box->x - (previous.box->x + previous.box->width);
        auto const height = std::max(1.0, std::min(previous.box->height, word.box->height));
        return gap >= 0.3 * height;
    }
    if (previous.text.isEmpty() or word.text.isEmpty()) return false;
    return not isCjk(previous.text.back()) and not isCjk(word.text.front());
}

QString joinWords(std::vector<OcrWord> const& words)
{
    QString out;
    OcrWord previous;
    bool    hasPrevious = false;
    for (auto const& word : words) {
        if (hasPrevious and separated(previous, word)) out += QLatin1Char(' ');
        out += word.text;
        previous    = word;
        hasPrevious = true;
    }
    return out.trimmed();
}

OcrResult failure(OcrStatus status)
{
    return {.status = status};
}

bool validBoxFields(int left, int top, int width, int height)
{
    return left >= 0 and top >= 0 and width > 0 and height > 0;
}

}

bool boxFitsImage(OcrBox const& box, QSize image, double tolerance)
{
    if (image.isEmpty() or not std::isfinite(box.x) or not std::isfinite(box.y) or
        not std::isfinite(box.width) or not std::isfinite(box.height))
        return false;
    if (box.x < -tolerance or box.y < -tolerance or box.width <= 0 or box.height <= 0) return false;
    return box.x + box.width <= image.width() + tolerance and box.y + box.height <= image.height() + tolerance;
}

OcrResult parseTesseractTsv(QByteArray const& tsv, QSize imageSize)
{
    if (imageSize.isEmpty()) return failure(OcrStatus::InvalidImage);
    if (tsv.size() > static_cast<qsizetype>(kMaxStructuredBytes)) return failure(OcrStatus::ResultTooLarge);
    if (tsv.startsWith("\xEF\xBB\xBF")) return failure(OcrStatus::ProtocolError);

    int const newline = tsv.indexOf('\n');
    if (newline < 0) return failure(OcrStatus::ProtocolError);
    auto header = tsv.left(newline);
    if (header.endsWith('\r')) header.chop(1);
    if (header != kHeader) return failure(OcrStatus::ProtocolError);

    struct OrderedLine {
        std::uint32_t page  = 0;
        std::uint32_t block = 0;
        std::uint32_t par   = 0;
        std::uint32_t line  = 0;
        LineAccum     accum;
    };
    std::vector<OrderedLine>            order;
    QHash<quint64, std::size_t>         index;
    QHash<quint64, std::vector<double>> scores;

    std::size_t rowCount  = 0;
    std::size_t wordCount = 0;
    qsizetype   position  = newline + 1;
    while (position < tsv.size()) {
        qsizetype end = tsv.indexOf('\n', position);
        if (end < 0) end = tsv.size();
        auto row = tsv.mid(position, end - position);
        position = end + 1;
        if (row.endsWith('\r')) row.chop(1);
        if (row.isEmpty()) continue;
        if (++rowCount > kMaxRows) return failure(OcrStatus::ResultTooLarge);

        auto const columns = row.split('\t');
        if (columns.size() != 12) return failure(OcrStatus::ProtocolError);

        bool      ok    = false;
        int const level = columns[0].toInt(&ok);
        if (not ok) return failure(OcrStatus::ProtocolError);
        int const page = columns[1].toInt(&ok);
        if (not ok) return failure(OcrStatus::ProtocolError);
        int const block = columns[2].toInt(&ok);
        if (not ok) return failure(OcrStatus::ProtocolError);
        int const par = columns[3].toInt(&ok);
        if (not ok) return failure(OcrStatus::ProtocolError);
        int const line = columns[4].toInt(&ok);
        if (not ok) return failure(OcrStatus::ProtocolError);
        int const word = columns[5].toInt(&ok);
        if (not ok) return failure(OcrStatus::ProtocolError);
        int const left = columns[6].toInt(&ok);
        if (not ok) return failure(OcrStatus::ProtocolError);
        int const top = columns[7].toInt(&ok);
        if (not ok) return failure(OcrStatus::ProtocolError);
        int const width = columns[8].toInt(&ok);
        if (not ok) return failure(OcrStatus::ProtocolError);
        int const height = columns[9].toInt(&ok);
        if (not ok) return failure(OcrStatus::ProtocolError);
        double const conf = columns[10].toDouble(&ok);
        if (not ok or not std::isfinite(conf) or conf < -1 or conf > 100) return failure(OcrStatus::ProtocolError);
        if (level < 1 or level > 5 or page < 0 or block < 0 or par < 0 or line < 0 or word < 0)
            return failure(OcrStatus::ProtocolError);
        if (page > 65535 or block > 65535 or par > 65535 or line > 65535)
            return failure(OcrStatus::ProtocolError);

        if (level != 4 and level != 5) continue;
        if (not validBoxFields(left, top, width, height)) return failure(OcrStatus::ProtocolError);
        auto const box = OcrBox{static_cast<double>(left),
                                static_cast<double>(top),
                                static_cast<double>(width),
                                static_cast<double>(height)};
        if (not boxFitsImage(box, imageSize)) return failure(OcrStatus::ProtocolError);

        quint64 const key = (static_cast<quint64>(page) << 48) | (static_cast<quint64>(block) << 32) |
                            (static_cast<quint64>(par) << 16) | static_cast<quint64>(line);
        auto it = index.find(key);
        if (it == index.end()) {
            if (order.size() >= kMaxLines) return failure(OcrStatus::ResultTooLarge);
            it = index.insert(key, order.size());
            order.push_back({.page  = static_cast<std::uint32_t>(page),
                             .block = static_cast<std::uint32_t>(block),
                             .par   = static_cast<std::uint32_t>(par),
                             .line  = static_cast<std::uint32_t>(line)});
        }
        auto& accum = order[*it].accum;

        if (level == 4) {
            accum.box = box;
            continue;
        }

        QString text;
        if (not decodeStrict(columns[11], text)) return failure(OcrStatus::ProtocolError);
        if (text.isEmpty()) continue;
        if (++wordCount > kMaxWords) return failure(OcrStatus::ResultTooLarge);
        if (conf >= 0) scores[key].push_back(conf);
        accum.words.push_back({.text = std::move(text), .box = box});
    }

    OcrResult   result{.status = OcrStatus::Ready};
    double      confidenceSum = 0;
    std::size_t scoredWords   = 0;
    std::size_t totalWords    = 0;

    for (auto& entry : order) {
        auto& accum = entry.accum;
        if (accum.words.empty() and not accum.box) continue;
        OcrLine lineEntry;
        lineEntry.box   = accum.box;
        lineEntry.words = std::move(accum.words);
        lineEntry.text  = joinWords(lineEntry.words);
        if (not lineEntry.text.isEmpty()) {
            totalWords += lineEntry.words.size();
            quint64 const key = (static_cast<quint64>(entry.page) << 48) | (static_cast<quint64>(entry.block) << 32) |
                                (static_cast<quint64>(entry.par) << 16) | static_cast<quint64>(entry.line);
            auto const scoreIt = scores.constFind(key);
            if (scoreIt != scores.cend()) {
                for (double const score : *scoreIt) {
                    confidenceSum += score / 100.0;
                    ++scoredWords;
                }
            }
            result.lines.push_back(std::move(lineEntry));
        }
    }

    if (result.lines.empty()) return failure(OcrStatus::EmptyText);
    for (auto const& line : result.lines) {
        if (not result.text.isEmpty()) result.text += QLatin1Char('\n');
        result.text += line.text;
    }
    result.text = result.text.trimmed();
    if (result.text.isEmpty()) return failure(OcrStatus::EmptyText);
    if (result.text.size() > static_cast<qsizetype>(kMaxTextUnits)) return failure(OcrStatus::ResultTooLarge);

    std::size_t structured = result.text.toUtf8().size();
    for (auto const& line : result.lines) {
        structured += line.text.toUtf8().size() + 32;
        for (auto const& word : line.words)
            structured += word.text.toUtf8().size() + 64;
    }
    if (structured > kMaxStructuredBytes) return failure(OcrStatus::ResultTooLarge);

    bool anyWordBox = false;
    bool anyLineBox = false;
    for (auto const& line : result.lines) {
        anyLineBox = anyLineBox or line.box.has_value();
        for (auto const& word : line.words)
            anyWordBox = anyWordBox or word.box.has_value();
    }
    result.geometryLevel = anyWordBox ? OcrGeometryLevel::Word : anyLineBox ? OcrGeometryLevel::Line
                                                                            : OcrGeometryLevel::None;

    if (scoredWords > 0)
        result.confidence = OcrConfidence{.average = confidenceSum / static_cast<double>(scoredWords),
                                          .words   = scoredWords,
                                          .total   = totalWords};
    return result;
}

}
