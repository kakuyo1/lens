/**
 * @file windows_ocr_probe_test.cpp
 * @brief The Windows.Media.Ocr deployment probe: identity, languages, and one synthetic recognition.
 *
 * Microsoft documents Windows.Media.Ocr for desktop apps with package identity. Lens ships as an
 * Inno Setup install and a portable ZIP, so whether the API actually works from this process is a
 * question only a run can answer -- and the answer decides whether the Windows backend may be
 * enabled. This case records the evidence; it does not install a language pack, read the desktop,
 * or send anything off the machine.
 *
 * The image is the committed synthetic corpus entry, not a screen capture. Only counts and the
 * deployment facts are printed; the recognized text itself stays out of the log.
 *
 * Human-run: `powershell -File scripts/qa/gtest.ps1 -Target lens_gtest_integration -Filter 'WindowsOcrProbe*'`.
 */

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <iostream>
#include <span>
#include <string>

#include <QCoreApplication>
#include <QImage>
#include <gtest/gtest.h>

#include <Windows.h>
#include <appmodel.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

namespace {

/// @return The package full name, or an empty string when the process has no package identity.
std::wstring packageIdentity()
{
    UINT32     length = 0;
    LONG const probe  = ::GetCurrentPackageFullName(&length, nullptr);
    if (probe != ERROR_INSUFFICIENT_BUFFER or length == 0) return {};
    std::wstring name(length, L'\0');
    if (::GetCurrentPackageFullName(&length, name.data()) != ERROR_SUCCESS) return {};
    name.resize(length > 0 ? length - 1 : 0);
    return name;
}

std::string narrow(std::wstring const& text)
{
    if (text.empty()) return {};
    int const   bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}

std::wstring widened(std::string const& text)
{
    if (text.empty()) return {};
    int const    chars = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(chars), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), chars);
    return out;
}

bool containsFolded(std::wstring const& haystack, std::wstring const& needle)
{
    // Windows OCR spaces Chinese runs apart, and the corpus transcription does not: whitespace is
    // not a difference for this check, only the characters are.
    auto fold = [](std::wstring text) {
        text.erase(std::remove_if(text.begin(), text.end(), [](wchar_t character) { return std::iswspace(character) != 0; }), text.end());
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t character) {
            return static_cast<wchar_t>(std::towlower(character));
        });
        return text;
    };
    return fold(haystack).find(fold(needle)) != std::wstring::npos;
}

}

TEST(WindowsOcrProbe, ReportsDeploymentAndRecognizesASyntheticImage)
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    auto const identity = packageIdentity();
    std::cout << "package identity: " << (identity.empty() ? "<none>" : narrow(identity)) << '\n';

    auto languages = winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages();
    std::cout << "recognizer languages: " << languages.Size() << '\n';
    std::wstring chosenTag;
    for (auto language : languages) {
        std::wstring const tag = language.LanguageTag().c_str();
        std::cout << "  " << narrow(tag) << '\n';
        if (chosenTag.empty()) chosenTag = tag;
        if (tag == L"en" or tag.rfind(L"en-", 0) == 0) {
            chosenTag = tag;
            break;
        }
    }
    std::cout << "max image dimension: " << winrt::Windows::Media::Ocr::OcrEngine::MaxImageDimension() << '\n';

    if (chosenTag.empty()) GTEST_SKIP() << "no recognizer is installed; nothing to recognize with";

    winrt::Windows::Media::Ocr::OcrEngine engine{nullptr};
    try {
        engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(
            winrt::Windows::Globalization::Language{chosenTag});
    } catch (winrt::hresult_error const& error) {
        std::cout << "TryCreateFromLanguage failed: 0x" << std::hex << static_cast<unsigned long>(error.code()) << std::dec << '\n';
    }
    if (not engine) {
        try {
            engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
        } catch (winrt::hresult_error const& error) {
            std::cout << "TryCreateFromUserProfileLanguages failed: 0x" << std::hex << static_cast<unsigned long>(error.code()) << std::dec << '\n';
        }
    }
    ASSERT_TRUE(engine) << "no recognizer could be created, though one is listed";

    bool const    english = chosenTag == L"en" or chosenTag.rfind(L"en-", 0) == 0;
    QString const corpus  = QString::fromUtf8(LENS_SOURCE_DIR) + QStringLiteral("/test/ocr/corpus/");
    QString const file    = english ? QStringLiteral("browser-en-100-light.png") : QStringLiteral("browser-zh-100-light.png");
    QImage        image{corpus + file};
    ASSERT_FALSE(image.isNull()) << "the synthetic corpus image is missing";
    image = image.convertToFormat(QImage::Format_ARGB32);
    ASSERT_FALSE(image.isNull());

    winrt::Windows::Storage::Streams::DataWriter writer;
    auto const                                   pixels = std::span<std::uint8_t const>{image.constBits(), static_cast<std::size_t>(image.sizeInBytes())};
    writer.WriteBytes(winrt::array_view<std::uint8_t const>{pixels.data(), pixels.data() + pixels.size()});
    auto const buffer = writer.DetachBuffer();
    auto const bitmap = winrt::Windows::Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(
        buffer,
        winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
        image.width(),
        image.height(),
        winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Premultiplied);

    winrt::Windows::Media::Ocr::OcrResult result{nullptr};
    try {
        result = engine.RecognizeAsync(bitmap).get();
    } catch (winrt::hresult_error const& error) {
        std::cout << "RecognizeAsync failed: 0x" << std::hex << static_cast<unsigned long>(error.code()) << std::dec << '\n';
        FAIL() << "recognizing a synthetic image threw";
    }
    ASSERT_TRUE(result);

    std::size_t words     = 0;
    std::size_t lineCount = 0;
    auto        ocrLines  = result.Lines();
    lineCount             = static_cast<std::size_t>(ocrLines.Size());
    for (std::uint32_t index = 0; index < ocrLines.Size(); ++index) {
        auto ocrLine  = ocrLines.GetAt(index);
        auto wordView = ocrLine.Words();
        words += static_cast<std::size_t>(wordView.Size());
    }
    std::wstring const text = result.Text().c_str();
    std::cout << "chosen language: " << narrow(chosenTag) << ", lines: " << lineCount
              << ", words: " << words << ", characters: " << text.size() << '\n';

    EXPECT_GT(lineCount, 0u);
    EXPECT_GT(words, 0u);
    EXPECT_TRUE(containsFolded(text, english ? L"harbour" : L"码头")) << "the recognizer did not return the synthetic sentence";
}
