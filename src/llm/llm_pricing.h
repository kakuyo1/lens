#pragma once

#include <QHash>
#include <QSet>
#include <QString>

#include <filesystem>

#include "llm_client.h" // Usage

/**
 * @file llm_pricing.h
 * @brief The price list as data: what a model charges per token, from `data/llm/pricing.json`.
 *
 * The cost surfaces show money, and the service reports only tokens, so the conversion has to
 * happen here. Keeping the numbers in a data file rather than in C++ means a price change is
 * a data edit -- the same reason the prompts and the response schema are data.
 *
 * @note A price list is not a billing statement. See cost() for what it deliberately leaves
 *       out.
 *
 * @note The vendor quotes in one currency and the surfaces show another: today's file has
 *       DeepSeek's rates in USD and a `display` block naming CNY with the rate, so a reader
 *       sees ¥ without converting. The vendor's own numbers stay untouched; a rate is a dated
 *       fact -- `asOf` says which day's -- and the rate itself is data, so updating it is a
 *       data edit like any other price.
 */

namespace lens::llm {

/// @brief What one model charges for each direction of traffic.
struct ModelPrice {
    double input  = 0.0; ///< Per `unit` prompt tokens.
    double output = 0.0; ///< Per `unit` completion tokens.
};

/**
 * @brief The price list, loaded once at startup.
 */
class Pricing {
public:
    /**
     * @brief Read the price list.
     *
     * @param path Normally `<repo>/data/llm/pricing.json`.
     * @throws std::runtime_error If the file is missing, is not valid JSON, states no
     *         currency or unit, lists no model, or carries a `display` block that names no
     *         currency or a rate that is not positive. A half-read price list is a startup
     *         fault: costing everything at zero would look like the feature working.
     */
    static Pricing load(const std::filesystem::path& path);

    /**
     * @brief What one call cost, in currency().
     *
     * @param model Model name as sent on the wire (`Config::model`).
     * @param usage Token counts from that call's response.
     * @return The amount in currency(), or 0 when the model is not in the list; an unlisted
     *         model must not suppress the rest of the tally.
     *
     * @note A model the list does not carry is reported once, the first time it is asked about,
     *       then left alone: the budget is re-evaluated twice a second and each evaluation
     *       prices the whole day's usage, so per-call reporting ran to 2 MB of log a day.
     *
     * @note The conversion lives here, and only here, so every downstream surface -- the
     *       statistics popup, the cost popup, the tray tooltip -- shows one currency.
     *
     * @note Two deliberate omissions: the service quotes peak and off-peak rates and this uses
     *       the off-peak one, understating a call in a peak window by up to 2x; and a prompt
     *       whose prefix was cached is billed far less, which the token counts alone do not
     *       distinguish.
     */
    double cost(const QString& model, const Usage& usage) const;

    /// @return The currency cost() returns amounts in, e.g. "CNY": the `display` block's when
    ///         the file carries one, the vendor's own otherwise.
    QString currency() const
    {
        return displayCurrency_;
    }

private:
    QString currency_        = QStringLiteral("USD");    ///< What the vendor quotes its rates in.
    QString displayCurrency_ = QStringLiteral("USD");    ///< What the surfaces show; the file's
                                                         ///< `display.currency`, or the vendor's.
    double                     displayMultiplier_ = 1.0; ///< currency_ -> displayCurrency_.
    QString                    rateAsOf_;                ///< The day the multiplier was taken.
    long long                  unit_ = 1000000;          ///< Token count the prices are per.
    QHash<QString, ModelPrice> byModel_;
    /// Models cost() has already reported as missing from the list. Mutable and written from a
    /// const method: it remembers what was said, it is not part of the list. Nothing synchronises
    /// it; the cost surfaces read it from the thread that owns them.
    mutable QSet<QString> unlistedReported_;
};

}
