/** @file ocr_engine.cpp
 * @brief Tesseract process adapter with bundled runtime resolution and bounded I/O.
 */
#include "ocr_engine.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>

#include "ocr_tsv.h"

namespace lens::capture {
namespace {

constexpr qsizetype kMaxOutput = 4 * 1024 * 1024;
constexpr qsizetype kMaxError  = 64 * 1024;

/// @return Whether @p path is a traineddata file Tesseract could actually load: an empty file is
///         what a half-finished download leaves behind, and passing it as a language fails the run.
bool isUsableTrainedData(QString const& path)
{
    const QFileInfo data{path};
    return data.isReadable() and data.size() > 0;
}

TesseractLanguageSelection selectionFor(TesseractConfig const& config)
{
    if (config.language) return tesseractLanguageFor(*config.language, config.dataDirectory);
    QString const legacy = tesseractLanguages(config.dataDirectory);
    return {.spec = legacy, .missing = legacy.isEmpty() ? QStringLiteral("eng") : QString{}};
}

QString psmFor(OcrLayoutHint const layout)
{
    switch (layout) {
        case OcrLayoutHint::Unknown: return QStringLiteral("3");
        case OcrLayoutHint::Word: return QStringLiteral("8");
        case OcrLayoutHint::Line: return QStringLiteral("7");
        case OcrLayoutHint::Block: return QStringLiteral("6");
        case OcrLayoutHint::Sparse: return QStringLiteral("11");
    }
    return QStringLiteral("3");
}

OcrResult withAttempt(OcrResult result, QString const engine, int const elapsedMs)
{
    result.attempts.push_back({.engine = std::move(engine), .status = result.status, .elapsedMs = elapsedMs});
    return result;
}

struct TesseractOcr final : OcrEngine {
    explicit TesseractOcr(TesseractConfig const& config_)
        : config{resolveTesseract(config_)}
    {}

    OcrStatus runtimeStatus() const
    {
        return config.executable.isEmpty() or not QFileInfo{config.executable}.isFile() ? OcrStatus::RuntimeMissing
                                                                                        : OcrStatus::Ready;
    }

    OcrStatus resourceStatus() const
    {
        auto const runtime = runtimeStatus();
        if (runtime != OcrStatus::Ready) return runtime;
        return selectionFor(config).ok() ? OcrStatus::Ready : OcrStatus::LanguageMissing;
    }

    OcrStatus probe(std::shared_ptr<std::atomic_bool> cancel = {}) const override
    {
        if (resourceStatus() != OcrStatus::Ready) return resourceStatus();
        QImage blank{64, 64, QImage::Format_Grayscale8};
        blank.fill(Qt::white);
        OcrRequest request{.source = OcrSource::LocalTest, .image = blank, .language = config.language};
        auto const result = recognize(request, std::move(cancel));
        return result.status == OcrStatus::EmptyText or result.status == OcrStatus::Ready ? OcrStatus::Ready : result.status;
    }

    OcrResult recognize(OcrRequest const& request, std::shared_ptr<std::atomic_bool> cancel = {}) const override
    {
        QElapsedTimer timer;
        timer.start();
        auto const started = [&](OcrStatus const status) {
            return withAttempt({.status = status, .actualEngine = QStringLiteral("tesseract"), .requestId = request.requestId},
                               QStringLiteral("tesseract"),
                               static_cast<int>(timer.elapsed()));
        };
        if (request.image.isNull()) return started(OcrStatus::InvalidImage);
        if (request.image.width() > 8192 or request.image.height() > 8192 or request.image.sizeInBytes() > 128 * 1024 * 1024)
            return started(OcrStatus::InputTooLarge);
        if (cancel and cancel->load(std::memory_order_relaxed)) return started(OcrStatus::Cancelled);
        auto const available = runtimeStatus();
        if (available != OcrStatus::Ready) return started(available);

        auto effective       = config;
        effective.language   = request.language ? request.language : config.language;
        auto const selection = selectionFor(effective);
        if (not selection.ok()) return started(OcrStatus::LanguageMissing);
        auto const timeout = std::min(config.timeoutMs, static_cast<int>(request.deadline.count()));
        if (timeout <= 0) return started(OcrStatus::TimedOut);

        QByteArray png;
        QBuffer    buffer{&png};
        if (not buffer.open(QIODevice::WriteOnly) or not request.image.save(&buffer, "PNG")) return started(OcrStatus::InvalidImage);

        QProcess process;
        process.start(config.executable,
                      {QStringLiteral("stdin"), QStringLiteral("stdout"), QStringLiteral("--tessdata-dir"), config.dataDirectory, QStringLiteral("-l"), selection.spec, QStringLiteral("--psm"), psmFor(request.layout), QStringLiteral("-c"), QStringLiteral("tessedit_create_tsv=1")});
        if (not process.waitForStarted(timeout))
            return started(OcrStatus::StartFailed);

        QByteArray output;
        qsizetype  errorBytes = 0;
        auto       drain      = [&] {
            const auto outputLimit = std::max<qsizetype>(1, kMaxOutput - output.size() + 1);
            process.setReadChannel(QProcess::StandardOutput);
            output += process.read(static_cast<qint64>(outputLimit));
            const auto errorLimit = std::max<qsizetype>(1, kMaxError - errorBytes + 1);
            process.setReadChannel(QProcess::StandardError);
            errorBytes += process.read(static_cast<qint64>(errorLimit)).size();
            process.setReadChannel(QProcess::StandardOutput);
            return output.size() <= kMaxOutput and errorBytes <= kMaxError;
        };
        auto const stop = [&](OcrStatus const status) {
            process.kill();
            process.waitForFinished(1000);
            return started(status);
        };

        qsizetype written = 0;
        while (written < png.size()) {
            if (cancel and cancel->load(std::memory_order_relaxed)) return stop(OcrStatus::Cancelled);
            auto const count = process.write(png.constData() + written, png.size() - written);
            if (count > 0) {
                written += count;
                if (not drain()) return stop(OcrStatus::ResultTooLarge);
                continue;
            }
            auto const remaining = std::max(1, timeout - static_cast<int>(timer.elapsed()));
            if (not process.waitForBytesWritten(std::min(remaining, 50))) return stop(OcrStatus::Failed);
        }
        process.closeWriteChannel();

        while (process.state() != QProcess::NotRunning) {
            if (cancel and cancel->load(std::memory_order_relaxed)) return stop(OcrStatus::Cancelled);
            if (not drain()) return stop(OcrStatus::ResultTooLarge);
            auto const elapsed = static_cast<int>(timer.elapsed());
            if (elapsed >= timeout) return stop(OcrStatus::TimedOut);
            process.setReadChannel(QProcess::StandardOutput);
            process.waitForReadyRead(std::min(timeout - elapsed, 10));
            process.setReadChannel(QProcess::StandardError);
            process.waitForReadyRead(0);
            process.setReadChannel(QProcess::StandardOutput);
        }
        if (not drain()) return started(OcrStatus::ResultTooLarge);
        if (process.exitStatus() != QProcess::NormalExit or process.exitCode() != 0)
            return started(OcrStatus::Failed);

        auto result                 = parseTesseractTsv(output, request.image.size());
        result.requestId            = request.requestId;
        result.actualEngine         = QStringLiteral("tesseract");
        result.languageUsed         = selection.spec;
        result.timing.recognitionMs = static_cast<int>(timer.elapsed());
        result.timing.totalMs       = result.timing.recognitionMs;
        return withAttempt(std::move(result), QStringLiteral("tesseract"), result.timing.recognitionMs);
    }

    TesseractConfig config;
};

}

QString ocrLanguageKey(OcrLanguage const language)
{
    switch (language) {
        case OcrLanguage::English: return QStringLiteral("en");
        case OcrLanguage::SimplifiedChinese: return QStringLiteral("zh-Hans");
        case OcrLanguage::ChineseEnglish: return QStringLiteral("zh-Hans+en");
    }
    return QStringLiteral("en");
}

std::optional<OcrLanguage> parseOcrLanguage(std::string_view const key)
{
    if (key == "en") return OcrLanguage::English;
    if (key == "zh-Hans") return OcrLanguage::SimplifiedChinese;
    if (key == "zh-Hans+en") return OcrLanguage::ChineseEnglish;
    return std::nullopt;
}

TesseractLanguageSelection tesseractLanguageFor(OcrLanguage const language, QString const& dataDirectory)
{
    auto const usable = [&](char const* code) {
        return isUsableTrainedData(QDir{dataDirectory}.filePath(QString::fromLatin1(code) + QStringLiteral(".traineddata")));
    };
    switch (language) {
        case OcrLanguage::English:
            return usable("eng") ? TesseractLanguageSelection{.spec = QStringLiteral("eng")}
                                 : TesseractLanguageSelection{.missing = QStringLiteral("eng")};
        case OcrLanguage::SimplifiedChinese:
            return usable("chi_sim") ? TesseractLanguageSelection{.spec = QStringLiteral("chi_sim")}
                                     : TesseractLanguageSelection{.missing = QStringLiteral("chi_sim")};
        case OcrLanguage::ChineseEnglish: {
            QStringList missing;
            if (not usable("chi_sim")) missing << QStringLiteral("chi_sim");
            if (not usable("eng")) missing << QStringLiteral("eng");
            if (not missing.isEmpty())
                return TesseractLanguageSelection{.missing = missing.join(QLatin1Char('+'))};
            return TesseractLanguageSelection{.spec = QStringLiteral("chi_sim+eng")};
        }
    }
    return {.missing = QStringLiteral("eng")};
}

QString tesseractLanguages(QString const& dataDirectory)
{
    // Chinese first: a reader who put chi_sim.traineddata in the directory did it to read Chinese,
    // and Tesseract weighs the languages in the order it is given them.
    QStringList available;
    for (auto const& code : {QStringLiteral("chi_sim"), QStringLiteral("eng")}) {
        if (isUsableTrainedData(QDir{dataDirectory}.filePath(code + QStringLiteral(".traineddata"))))
            available << code;
    }
    return available.join(QLatin1Char('+'));
}

TesseractConfig resolveTesseract(TesseractConfig config)
{
    config.timeoutMs = std::clamp(config.timeoutMs, 100, 30000);
    if (config.executable.isEmpty()) {
        auto const bundled = QDir{QCoreApplication::applicationDirPath()}.filePath(QStringLiteral("ocr/tesseract.exe"));
        config.executable  = QFileInfo::exists(bundled) ? bundled : QString{};
#ifdef LENS_OCR_DEV_PATH_FALLBACK
        if (config.executable.isEmpty())
            config.executable = QStandardPaths::findExecutable(QStringLiteral("tesseract"));
#endif
    }
    if (config.dataDirectory.isEmpty() and not config.executable.isEmpty())
        config.dataDirectory = QFileInfo{config.executable}.dir().filePath(QStringLiteral("tessdata"));
    return config;
}

std::unique_ptr<OcrEngine> makeTesseractOcr(TesseractConfig const& config)
{
    return std::make_unique<TesseractOcr>(config);
}

std::uint64_t pixelFingerprint(QImage const& image)
{
    auto const pixels = image.convertToFormat(QImage::Format_Grayscale8);
    auto       hash   = std::uint64_t{14695981039346656037ULL};
    for (int y = 0; y < pixels.height(); ++y) {
        auto const* row = pixels.constScanLine(y);
        for (int x = 0; x < pixels.width(); ++x) {
            hash ^= row[x];
            hash *= 1099511628211ULL;
        }
    }
    hash ^= static_cast<std::uint64_t>(pixels.width());
    hash *= 1099511628211ULL;
    hash ^= static_cast<std::uint64_t>(pixels.height());
    return hash;
}

}
