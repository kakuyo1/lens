/**
 * @file translation.cpp
 * @brief Loading the interface .qm and keeping one installed: see translation.h.
 */

#include "translation.h"

#include <QCoreApplication>

#include "util/log.h"

namespace lens::app {
namespace {

/// @brief The setting's language code, and the locale whose .qm carries it.
///
/// Not the same string: the setting holds "zh", the file is named for the locale, lens_zh_CN.qm;
/// "zh-Hant" is the script-tagged code the catalog offers, and its file is lens_zh_TW.qm. A name
/// built straight from the setting would look for lens_zh.qm or lens_zh-Hant.qm, neither of which
/// exists.
///
/// One code per .ts under i18n/, also the list `data/llm/catalog.json` offers as interface
/// languages: a language the catalog names and this function does not silently leaves the
/// interface in English.
const char* translationFileFor(const QString& language)
{
    if (language == QLatin1String("zh"))
        return "zh_CN";
    if (language == QLatin1String("zh-Hant"))
        return "zh_TW";
    if (language == QLatin1String("en"))
        return "en_US";
    if (language == QLatin1String("es"))
        return "es_ES";
    if (language == QLatin1String("ja"))
        return "ja_JP";
    return nullptr;
}

} // namespace

std::unique_ptr<QTranslator> loadTranslation(const QString& language)
{
    const char* file = translationFileFor(language);
    if (file == nullptr) {
        LENS_WARN("'{}' names no translation file; the interface stays in English",
                  language.toStdString());
        return {};
    }

    auto translator = std::make_unique<QTranslator>();
    if (!translator->load(QStringLiteral(":/i18n/lens_%1.qm").arg(QLatin1String(file)))) {
        LENS_WARN("lens_{}.qm did not load; the interface stays in English", file);
        return {};
    }
    return translator;
}

void TranslationKeeper::select(const QString& language)
{
    if (current_ == language)
        return;
    if (translator_)
        QCoreApplication::removeTranslator(translator_.get());
    translator_ = loadTranslation(language);
    if (translator_)
        QCoreApplication::installTranslator(translator_.get());
    current_ = language;
    LENS_INFO("interface language is now '{}'", language.toStdString());
}

} // namespace lens::app