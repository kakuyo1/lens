/**
 * @file rapidocr_bundle.cpp
 * @brief The pinned RapidOCR-json distribution and its on-disk state (docs/adr/0149).
 */

#include "rapidocr_bundle.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

namespace lens::capture {

namespace {

// The seven files the detector reads and the trust digest covers: the executable and, for both
// languages the adapter can be asked for, the detection, classification, recognition and keys
// files (rapidOcrModelFiles()). Sizes and digests were read from the unpacked v0.2.0 release.
BundleFile const bundleFiles[]{
    {QStringLiteral("RapidOCR-json.exe"), 16849920, QStringLiteral("616bd43a4672ee9fcf4d1ca607d7d9ec413b45d489482064d97e00a7efa9d76e")},
    {QStringLiteral("models/ch_PP-OCRv3_det_infer.onnx"), 2432880, QStringLiteral("3439588c030faea393a54515f51e983d8e155b19a2e8aba7891934c1cf0de526")},
    {QStringLiteral("models/ch_ppocr_mobile_v2.0_cls_infer.onnx"), 585532, QStringLiteral("e47acedf663230f8863ff1ab0e64dd2d82b838fceb5957146dab185a89d6215c")},
    {QStringLiteral("models/rec_en_PP-OCRv3_infer.onnx"), 8971646, QStringLiteral("6933432cf1754771a490a72a44a5bd513008e5b772f6da71c33726636ee812b0")},
    {QStringLiteral("models/dict_en.txt"), 190, QStringLiteral("5662df9d2d03f0e8ca0d3b0649d6acbab904b6a14b3d3521463c71c37c668ce3")},
    {QStringLiteral("models/ch_PP-OCRv3_rec_infer.onnx"), 10690752, QStringLiteral("897a3ededb38fee0dae2c1ccee38241f37df202c9509e3abca02e9217c5ee615")},
    {QStringLiteral("models/dict_chinese.txt"), 26249, QStringLiteral("28b2362ad4ab2dc38769aa72feb535e3a9ddb3fd2a7585a05920e6393b1dc7f7")},
};

}

BundleSpec const& bundleSpec()
{
    // The v0.2.0 release the adapter serves: its only asset, audited in
    // docs/metrics/ocr-baseline-2026-10-28.md section 2.
    static const BundleSpec spec{
        QStringLiteral("https://github.com/hiroi-sora/RapidOCR-json/releases/download/v0.2.0/RapidOCR-json_v0.2.0.7z"),
        73461693,
        QStringLiteral("7ad9b283d03436c6cd0296723188699299cb4e5cf9140b410c59543aa5793c40"),
        QStringLiteral("RapidOCR-json_v0.2.0"),
        bundleFiles};
    return spec;
}

std::filesystem::path bundleRoot()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return std::filesystem::path{base.toStdWString()} / L"rapidocr";
}

std::filesystem::path bundleFolder()
{
    return bundleRoot() / bundleSpec().innerFolder.toStdWString();
}

std::filesystem::path bundleExecutablePath()
{
    return bundleFolder() / L"RapidOCR-json.exe";
}

std::filesystem::path bundleModelsDirectoryPath()
{
    return bundleFolder() / L"models";
}

std::filesystem::path bundleArchivePath()
{
    return bundleRoot() / QUrl{bundleSpec().url}.fileName().toStdWString();
}

bool bundleInstalled(std::filesystem::path const& root)
{
    for (BundleFile const& file : bundleSpec().files) {
        QFileInfo const source{QString::fromStdWString(
            (root / bundleSpec().innerFolder.toStdWString() / file.name.toStdWString()).wstring())};
        if (not source.isFile() or source.size() != file.size)
            return false;
    }
    return true;
}

bool verifyBundleFile(QString const& path, BundleFile const& file)
{
    QFile source{path};
    if (not source.open(QIODevice::ReadOnly) or source.size() != file.size)
        return false;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    while (not source.atEnd())
        hash.addData(source.read(1 << 20));
    return hash.result().toHex() == file.sha256.toLatin1();
}

}
