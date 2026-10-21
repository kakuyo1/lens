#pragma once

#include <QString>

#include <string_view>

#include "bubble_settings.h"
#include "core/document/known_store.h"
#include "core/domain/review_schedule.h"
#include "glass_settings.h"
#include "llm/llm_protocol.h"
#include "reveal_settings.h"

/**
 * @file settings_defaults.h
 * @brief The values a settings document that names nothing reads as, in one place.
 *
 * Both readers of a default meet here: the settings projection falls back to these values, and
 * restore-defaults writes them. A default that lived only in the QML restore drifted from the
 * getter's fallback; moving both to one header is what keeps a fresh install and a restore
 * saying the same thing.
 *
 * The values a reader owns rather than a setting -- the API key, autostart, the interface
 * language, the daily budget -- are deliberately absent: they are not this table's to restore.
 */

namespace lens::app::defaults {

inline constexpr int                   kLevel                    = core::KnownStore::kDefaultLevel;
inline constexpr std::string_view      kExplanationLang          = core::KnownStore::kDefaultExplanationLang;
inline constexpr bool                  kMultiSense               = false;
inline constexpr bool                  kEtymology                = false;
inline constexpr bool                  kExample                  = false;
inline constexpr std::string_view      kTheme                    = "light";
inline constexpr bool                  kAnimationsEnabled        = true;
inline constexpr int                   kRevealSpeed              = ::lens::app::kRevealSpeedDefault;
inline constexpr bool                  kSelectionCapture         = true;
inline constexpr bool                  kClipboardSilent          = false;
inline constexpr int                   kBubbleDismissSeconds     = ::lens::app::kBubbleDismissDefaultSeconds;
inline constexpr int                   kBubbleFontSize           = ::lens::app::kBubbleFontSizeDefault;
inline constexpr std::string_view      kBubblePlacement          = "above";
inline constexpr std::string_view      kSelectionBarStyle        = "bar";
inline constexpr std::string_view      kUiLanguage               = "zh";
inline constexpr bool                  kOcrCapture               = false;
inline constexpr bool                  kAutoScan                 = false;
inline constexpr int                   kMinimumWordLength        = 3;
inline constexpr std::string_view      kScanWhitelist            = "chrome.exe;msedge.exe;firefox.exe;acrord32.exe";
inline constexpr core::ReviewFrequency kReviewFrequency          = core::kDefaultReviewFrequency;
inline constexpr int                   kFrostedBlurDefault       = kFrostedBlur.fallback;
inline constexpr int                   kFrostedSaturationDefault = kFrostedSaturation.fallback;
inline constexpr int                   kFrostedRimDefault        = kFrostedRim.fallback;
inline constexpr int                   kLiquidRefractionDefault  = kLiquidRefraction.fallback;
inline constexpr int                   kLiquidBlurDefault        = kLiquidBlur.fallback;
inline constexpr int                   kLiquidRimDefault         = kLiquidRim.fallback;
inline constexpr std::string_view      kLiquidTextColor          = kLiquidTextColorFallback;
inline constexpr int                   kGlassQuality             = ::lens::app::kGlassQualityDefault;

/// @return The provider a document that names none asks, from the shared catalogue.
inline QString defaultProviderName()
{
    return llm::serviceCatalog().value("defaultProvider").toString();
}

/// @return The first model the default provider's seed lists; empty when it has no seed.
inline QString defaultModelName()
{
    const auto models = llm::serviceProvider(defaultProviderName()).value("models").toArray();
    return models.isEmpty() ? QString{} : models.first().toString();
}

/// @return The default provider's endpoint, as the shared catalogue names it.
inline QString defaultBaseUrl()
{
    return llm::serviceProvider(defaultProviderName()).value("baseUrl").toString();
}

}
