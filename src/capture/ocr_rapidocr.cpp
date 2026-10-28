/** @file ocr_rapidocr.cpp
 * @brief RapidOCR-json v0.2.0: protocol, trust digest and one-process-per-task execution.
 */
#include "ocr_rapidocr.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QStringList>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <optional>

namespace lens::capture {
namespace {

constexpr qsizetype   kMaxOutput       = 4 * 1024 * 1024;
constexpr qsizetype   kMaxError        = 64 * 1024;
constexpr qsizetype   kMaxTextUnits    = 65536;
constexpr qsizetype   kMaxBlocks       = 2000;
constexpr qint64      kMaxTrustedBytes = 64LL * 1024 * 1024;
constexpr char const* kEngineName      = "rapidocr-json";
constexpr char const* kDetModel        = "ch_PP-OCRv3_det_infer.onnx";
constexpr char const* kClsModel        = "ch_ppocr_mobile_v2.0_cls_infer.onnx";
constexpr char const* kChineseRec      = "ch_PP-OCRv3_rec_infer.onnx";
constexpr char const* kEnglishRec      = "rec_en_PP-OCRv3_infer.onnx";
constexpr char const* kChineseKeys     = "dict_chinese.txt";
constexpr char const* kEnglishKeys     = "dict_en.txt";

/// @return Whether the path is an absolute local path rather than a URL, UNC share or relative one.
bool isLocalAbsolute(QString const& path)
{
    return not path.isEmpty() and QDir::isAbsolutePath(path) and not path.startsWith(QStringLiteral("//")) and
           not path.startsWith(QStringLiteral("\\\\"));
}

bool isRegularReadableFile(QString const& path)
{
    QFileInfo const info{path};
    return info.isFile() and info.isReadable() and info.size() > 0 and info.size() <= kMaxTrustedBytes;
}

QString modelPath(RapidOcrConfig const& config, QString const& name)
{
    return QDir{config.modelsDirectory}.filePath(name);
}

QString snapshotOf(RapidOcrConfig const& config)
{
    QStringList parts;
    auto const  append = [&parts](QString const& path) {
        QFileInfo const info{path};
        parts << QDir::cleanPath(info.absoluteFilePath()) << QString::number(info.size())
              << QString::number(info.lastModified().toMSecsSinceEpoch());
    };
    append(config.executable);
    for (auto const& name : rapidOcrModelFiles(config.language))
        append(modelPath(config, name));
    return parts.join(QLatin1Char('|'));
}

QStringList argumentsFor(RapidOcrConfig const& config)
{
    auto const files = rapidOcrModelFiles(config.language);
    return {QStringLiteral("--models"),
            config.modelsDirectory,
            QStringLiteral("--det"),
            files.at(0),
            QStringLiteral("--cls"),
            files.at(1),
            QStringLiteral("--rec"),
            files.at(2),
            QStringLiteral("--keys"),
            files.at(3),
            QStringLiteral("--ensureAscii=1"),
            QStringLiteral("--ensureLogger=0"),
            QStringLiteral("--numThread=2"),
            QStringLiteral("--padding=50"),
            QStringLiteral("--maxSideLen=1024"),
            QStringLiteral("--boxScoreThresh=0.5"),
            QStringLiteral("--boxThresh=0.3"),
            QStringLiteral("--unClipRatio=1.6"),
            QStringLiteral("--doAngle=1"),
            QStringLiteral("--mostAngle=1"),
            QStringLiteral("--GPU=-1")};
}

OcrResult failure(OcrStatus status, std::uint64_t requestId, QString const& language, int elapsedMs)
{
    OcrResult result;
    result.status               = status;
    result.requestId            = requestId;
    result.actualEngine         = QString::fromLatin1(kEngineName);
    result.languageUsed         = language;
    result.timing.recognitionMs = elapsedMs;
    result.timing.totalMs       = elapsedMs;
    return result;
}

/// Kills and reaps the child on every exit, including the successful one: this program has no exit
/// command and spins on stdin EOF.
struct KillOnExit {
    QProcess& process;
    ~KillOnExit()
    {
        if (process.state() == QProcess::NotRunning) return;
        process.kill();
        process.waitForFinished(2000);
    }
};

struct RapidOcrEngine final : OcrEngine {
    explicit RapidOcrEngine(RapidOcrConfig const& config_)
        : config{config_}, snapshot{snapshotOf(config_)}
    {}

    OcrStatus resourceStatus() const
    {
        auto resourceConfig         = config;
        resourceConfig.requireTrust = false;
        return inspectRapidOcr(resourceConfig).status;
    }

    OcrStatus checkTrust() const
    {
        if (not config.requireTrust) return OcrStatus::Ready;
        auto const current = snapshotOf(config);
        if (trustChecked and current == snapshot) return trustStatus;
        snapshot     = current;
        trustChecked = true;
        trustStatus  = OcrStatus::Untrusted;
        if (config.trustDigest.isEmpty()) return trustStatus;
        auto const digest = rapidOcrTrustDigest(config);
        if (digest.isEmpty()) {
            trustStatus = OcrStatus::RuntimeMissing;
            return trustStatus;
        }
        trustStatus = digest == config.trustDigest ? OcrStatus::Ready : OcrStatus::Untrusted;
        return trustStatus;
    }

    OcrStatus probe(std::shared_ptr<std::atomic_bool>) const override
    {
        auto const resource = resourceStatus();
        return resource == OcrStatus::Ready ? checkTrust() : resource;
    }

    OcrResult recognize(OcrRequest const& request, std::shared_ptr<std::atomic_bool> cancel = {}) const override
    {
        QElapsedTimer timer;
        timer.start();
        auto const finish = [&](OcrStatus const status) {
            return failure(status, request.requestId, ocrLanguageKey(request.language.value_or(config.language)), static_cast<int>(timer.elapsed()));
        };
        if (request.image.isNull()) return finish(OcrStatus::InvalidImage);
        if (request.image.width() > 8192 or request.image.height() > 8192 or request.image.sizeInBytes() > 128 * 1024 * 1024)
            return finish(OcrStatus::InputTooLarge);
        if (cancel and cancel->load(std::memory_order_relaxed)) return finish(OcrStatus::Cancelled);

        auto const resource = resourceStatus();
        if (resource != OcrStatus::Ready) return finish(resource);
        auto const trusted = checkTrust();
        if (trusted != OcrStatus::Ready) return finish(trusted);
        if (request.deadline.count() <= 0 or config.timeoutMs <= 0) return finish(OcrStatus::TimedOut);
        auto const timeout = std::min(config.timeoutMs, static_cast<int>(request.deadline.count()));

        QByteArray png;
        QBuffer    buffer{&png};
        if (not buffer.open(QIODevice::WriteOnly) or not request.image.save(&buffer, "PNG")) return finish(OcrStatus::InvalidImage);

        QProcess   process;
        KillOnExit guard{process};
        process.setProgram(config.executable);
        process.setArguments(argumentsFor(config));
        process.setWorkingDirectory(QFileInfo{config.executable}.absolutePath());
        process.start();
        if (not process.waitForStarted(timeout)) return finish(OcrStatus::StartFailed);

        QByteArray output;
        qint64     errorBytes = 0;
        auto const drain      = [&] {
            process.setReadChannel(QProcess::StandardOutput);
            output += process.read(std::max<qint64>(1, kMaxOutput - output.size() + 1));
            process.setReadChannel(QProcess::StandardError);
            errorBytes += process.read(std::max<qint64>(1, kMaxError - errorBytes + 1)).size();
            process.setReadChannel(QProcess::StandardOutput);
            return output.size() <= kMaxOutput and errorBytes <= kMaxError;
        };
        auto const takeLine = [&](QByteArray& line) -> std::optional<OcrStatus> {
            for (;;) {
                auto const newline = output.indexOf('\n');
                if (newline >= 0) {
                    line = output.left(newline);
                    output.remove(0, newline + 1);
                    if (line.endsWith('\r')) line.chop(1);
                    return std::nullopt;
                }
                if (cancel and cancel->load(std::memory_order_relaxed)) return OcrStatus::Cancelled;
                if (timer.elapsed() >= timeout) return OcrStatus::TimedOut;
                if (not drain()) return OcrStatus::ResultTooLarge;
                if (process.state() == QProcess::NotRunning) {
                    if (output.indexOf('\n') < 0) return OcrStatus::Failed;
                    continue;
                }
                process.waitForReadyRead(10);
            }
        };

        bool initialized = false;
        for (int lineCount = 0; lineCount < 8 and not initialized; ++lineCount) {
            QByteArray line;
            if (auto const status = takeLine(line))
                return finish(*status == OcrStatus::Failed ? OcrStatus::StartFailed : *status);
            initialized = line.contains("OCR init completed.");
        }
        if (not initialized) return finish(OcrStatus::StartFailed);

        QByteArray const payload = "{\"image_base64\":\"" + png.toBase64() + "\"}\n";
        qsizetype        written = 0;
        while (written < payload.size()) {
            if (cancel and cancel->load(std::memory_order_relaxed)) return finish(OcrStatus::Cancelled);
            if (process.state() == QProcess::NotRunning) return finish(OcrStatus::Failed);
            auto const count = process.write(payload.constData() + written, payload.size() - written);
            if (count < 0) return finish(OcrStatus::Failed);
            if (count > 0) {
                written += count;
                if (not drain()) return finish(OcrStatus::ResultTooLarge);
                continue;
            }
            if (timer.elapsed() >= timeout) return finish(OcrStatus::TimedOut);
            process.waitForBytesWritten(50);
        }

        QByteArray response;
        if (auto const status = takeLine(response)) return finish(*status);
        process.kill();
        process.waitForFinished(2000);

        auto result = parseRapidOcrResponse(response,
                                            request.image.size(),
                                            request.requestId,
                                            ocrLanguageKey(request.language.value_or(config.language)),
                                            QString::fromLatin1(kEngineName),
                                            static_cast<int>(timer.elapsed()));
        result.attempts.push_back({.engine    = QString::fromLatin1(kEngineName),
                                   .status    = result.status,
                                   .elapsedMs = static_cast<int>(timer.elapsed())});
        return result;
    }

    RapidOcrConfig    config;
    mutable bool      trustChecked = false;
    mutable OcrStatus trustStatus  = OcrStatus::Untrusted;
    mutable QString   snapshot;
};

}

QStringList rapidOcrModelFiles(OcrLanguage const language)
{
    QStringList files{QString::fromLatin1(kDetModel), QString::fromLatin1(kClsModel)};
    if (language == OcrLanguage::English) {
        files << QString::fromLatin1(kEnglishRec) << QString::fromLatin1(kEnglishKeys);
    } else {
        // The English dictionary is the one that matches rec_en; models/configs.txt names
        // dict_chinese for English, which the binary turns into Chinese glyphs.
        files << QString::fromLatin1(kChineseRec) << QString::fromLatin1(kChineseKeys);
    }
    return files;
}

bool rapidOcrPathIsAscii(QString const& path)
{
    for (auto const character : path)
        if (character.unicode() > 0x7F) return false;
    return true;
}

QString rapidOcrTrustDigest(RapidOcrConfig const& config)
{
    if (not isLocalAbsolute(config.executable) or not isLocalAbsolute(config.modelsDirectory)) return {};
    QCryptographicHash hash{QCryptographicHash::Sha256};
    hash.addData(QByteArrayLiteral("lens-rapidocr-v0.2.0"));
    auto const addFile = [&hash](QString const& path) {
        QFileInfo const info{path};
        if (not isRegularReadableFile(path)) return false;
        hash.addData(QDir::cleanPath(info.absoluteFilePath()).toUtf8());
        hash.addData("\0", 1);
        QFile file{path};
        if (not file.open(QIODevice::ReadOnly)) return false;
        auto const bytes = file.readAll();
        if (bytes.size() != info.size()) return false;
        hash.addData(bytes);
        return true;
    };
    if (not addFile(config.executable)) return {};
    // Include the folder itself so moving the same files invalidates a previous trust record.
    hash.addData(QDir::cleanPath(QDir{config.modelsDirectory}.absolutePath()).toUtf8());
    hash.addData("\0", 1);
    for (auto const& name : rapidOcrModelFiles(config.language))
        if (not addFile(modelPath(config, name))) return {};
    return QString::fromLatin1(hash.result().toHex());
}

RapidOcrInspection inspectRapidOcr(RapidOcrConfig const& config, bool const verifyTrust)
{
    QFileInfo const executable{config.executable};
    if (not isLocalAbsolute(config.executable) or not executable.isFile() or not executable.isReadable() or
        executable.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0)
        return {.status = OcrStatus::RuntimeMissing, .reason = QStringLiteral("runtime-missing")};

    QFileInfo const models{config.modelsDirectory};
    if (not isLocalAbsolute(config.modelsDirectory) or not models.isDir() or not rapidOcrPathIsAscii(config.modelsDirectory))
        return {.status = OcrStatus::PathUnsupported, .reason = QStringLiteral("path-unsupported")};

    QStringList missing;
    for (auto const& name : rapidOcrModelFiles(config.language)) {
        if (not isRegularReadableFile(modelPath(config, name))) missing << name;
    }
    if (not missing.isEmpty())
        return {.status = OcrStatus::LanguageMissing, .reason = missing.join(QStringLiteral(", "))};

    if (config.requireTrust) {
        if (config.trustDigest.isEmpty()) return {.status = OcrStatus::Untrusted, .reason = QStringLiteral("untrusted")};
        if (verifyTrust) {
            auto const digest = rapidOcrTrustDigest(config);
            if (digest.isEmpty()) return {.status = OcrStatus::RuntimeMissing, .reason = QStringLiteral("runtime-missing")};
            if (digest != config.trustDigest) return {.status = OcrStatus::Untrusted, .reason = QStringLiteral("trust-changed")};
        }
    }
    return {.status = OcrStatus::Ready, .reason = {}};
}

OcrResult parseRapidOcrResponse(QByteArray const&   response,
                                QSize const         imageSize,
                                std::uint64_t const requestId,
                                QString const&      language,
                                QString const&      engine,
                                int const           elapsedMs)
{
    auto const failureFor = [&](OcrStatus const status) {
        auto result         = failure(status, requestId, language, elapsedMs);
        result.actualEngine = engine;
        return result;
    };
    if (imageSize.isEmpty()) return failureFor(OcrStatus::InvalidImage);
    if (response.size() > kMaxOutput) return failureFor(OcrStatus::ResultTooLarge);

    QJsonParseError error{};
    auto const      document = QJsonDocument::fromJson(response, &error);
    if (error.error != QJsonParseError::NoError or not document.isObject()) return failureFor(OcrStatus::ProtocolError);
    auto const object = document.object();

    auto const codeValue = object.value(QStringLiteral("code"));
    if (not codeValue.isDouble()) return failureFor(OcrStatus::ProtocolError);
    auto const codeNumber = codeValue.toDouble();
    if (not std::isfinite(codeNumber) or codeNumber != std::floor(codeNumber)) return failureFor(OcrStatus::ProtocolError);
    auto const code = static_cast<int>(codeNumber);
    if (code == 101) return failureFor(OcrStatus::EmptyText);
    if (code != 100) return failureFor(OcrStatus::ProtocolError);

    auto const dataValue = object.value(QStringLiteral("data"));
    if (not dataValue.isArray()) return failureFor(OcrStatus::ProtocolError);
    auto const blocks = dataValue.toArray();
    if (blocks.size() > kMaxBlocks) return failureFor(OcrStatus::ResultTooLarge);

    OcrResult   result;
    QStringList texts;
    qsizetype   textUnits = 0;
    for (auto const& blockValue : blocks) {
        if (not blockValue.isObject()) return failureFor(OcrStatus::ProtocolError);
        auto const block     = blockValue.toObject();
        auto const textValue = block.value(QStringLiteral("text"));
        if (not textValue.isString()) return failureFor(OcrStatus::ProtocolError);
        auto const text = textValue.toString();
        if (text.isEmpty()) continue;
        textUnits += text.size();
        if (textUnits > kMaxTextUnits) return failureFor(OcrStatus::ResultTooLarge);

        auto const boxValue = block.value(QStringLiteral("box"));
        if (not boxValue.isArray()) return failureFor(OcrStatus::ProtocolError);
        auto const points = boxValue.toArray();
        if (points.size() != 4) return failureFor(OcrStatus::ProtocolError);
        double minX = std::numeric_limits<double>::max();
        double minY = minX;
        double maxX = std::numeric_limits<double>::lowest();
        double maxY = maxX;
        for (auto const& pointValue : points) {
            if (not pointValue.isArray()) return failureFor(OcrStatus::ProtocolError);
            auto const point = pointValue.toArray();
            if (point.size() != 2 or not point.at(0).isDouble() or not point.at(1).isDouble())
                return failureFor(OcrStatus::ProtocolError);
            auto const x = point.at(0).toDouble();
            auto const y = point.at(1).toDouble();
            if (not std::isfinite(x) or not std::isfinite(y)) return failureFor(OcrStatus::ProtocolError);
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
        OcrBox const box{.x = minX, .y = minY, .width = maxX - minX, .height = maxY - minY};
        if (box.width <= 0 or box.height <= 0 or not boxFitsImage(box, imageSize, 1.0))
            return failureFor(OcrStatus::ProtocolError);

        auto const scoreValue = block.value(QStringLiteral("score"));
        if (not scoreValue.isDouble()) return failureFor(OcrStatus::ProtocolError);
        auto const score = scoreValue.toDouble();
        if (not std::isfinite(score) or score < 0 or score > 1) return failureFor(OcrStatus::ProtocolError);

        OcrLine line;
        line.text = text;
        line.box  = box;
        result.lines.push_back(std::move(line));
        texts << text;
    }
    if (result.lines.empty()) return failureFor(OcrStatus::EmptyText);

    result.status = OcrStatus::Ready;
    result.text   = texts.join(QLatin1Char('\n'));
    if (result.text.size() > kMaxTextUnits) return failureFor(OcrStatus::ResultTooLarge);
    result.languageUsed         = language;
    result.actualEngine         = engine;
    result.requestId            = requestId;
    result.geometryLevel        = OcrGeometryLevel::Line;
    result.timing.recognitionMs = elapsedMs;
    result.timing.totalMs       = elapsedMs;
    return result;
}

std::unique_ptr<OcrEngine> makeRapidOcr(RapidOcrConfig const& config)
{
    return std::make_unique<RapidOcrEngine>(config);
}

}
