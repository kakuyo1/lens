/**
 * @file llm_pricing_test.cpp
 * @brief The price list: it loads, it refuses a broken one, it converts tokens to money, and it
 *        says a model is missing from it once.
 *
 * The rates pin data/llm/pricing.json, so a data edit that drops a model or a currency fails
 * here instead of quietly showing zero on the cost surface.
 */

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include "llm/llm_pricing.h"
#include "support.h"

namespace {

using lens::llm::Pricing;
using lens::llm::Usage;

std::filesystem::path pricingPath()
{
    return lens::test::sourceDir() / "data" / "llm" / "pricing.json";
}

/// @brief A temp file holding @p body, removed when the test ends.
struct TempPricing {
    explicit TempPricing(const char* body)
        : path(std::filesystem::temp_directory_path() / "lens_pricing_test.json")
    {
        std::ofstream(path) << body;
    }
    ~TempPricing()
    {
        std::filesystem::remove(path);
    }
    std::filesystem::path path;
};

/**
 * @brief Keeps the lines the process-wide logger emits while it stands, and counts them.
 *
 * The price list's only observable side effect is what it says, so the log is the seam to read
 * it at: log_test.cpp already treats the process-wide logger as the place init() is observable,
 * and this is the same logger the LENS_* macros reach. The level is forced down for the object's
 * lifetime and put back on the way out, so a case here cannot be silenced by whatever a case in
 * another file left the level at.
 */
class LogSpy {
public:
    LogSpy()
        : sink_(std::make_shared<Counting>())
    {
        auto logger = spdlog::default_logger();
        previous_   = logger->level();
        logger->set_level(spdlog::level::warn);
        logger->sinks().push_back(sink_);
    }

    ~LogSpy()
    {
        auto logger = spdlog::default_logger();
        logger->sinks().pop_back();
        logger->set_level(previous_);
    }

    /// @return How many lines logged so far carry @p needle.
    int count(std::string const& needle) const
    {
        int found = 0;
        for (const std::string& line : sink_->lines)
            if (line.find(needle) != std::string::npos)
                ++found;
        return found;
    }

private:
    struct Counting final : spdlog::sinks::base_sink<std::mutex> {
        std::vector<std::string> lines;
        void sink_it_(const spdlog::details::log_msg& msg) override
        {
            lines.emplace_back(msg.payload.data(), msg.payload.size());
        }
        void flush_() override
        {
        }
    };

    std::shared_ptr<Counting> sink_;
    spdlog::level::level_enum previous_ = spdlog::level::info;
};

}

TEST(PricingTest, LoadsTheShippedPriceList)
{
    const Pricing pricing = Pricing::load(pricingPath());

    // The vendor quotes in USD and the surfaces show CNY, so both numbers below are the
    // converted ones: this is what the cost popup prints.
    EXPECT_EQ(pricing.currency(), QStringLiteral("CNY"));
    // 1M prompt tokens at 0.15 per million, plus 1M completion at 0.6, at the file's rate.
    const double cost = pricing.cost(QStringLiteral("deepseek-flash"), Usage{1000000, 1000000});
    EXPECT_DOUBLE_EQ(cost, (0.15 + 0.6) * 6.71);
}

TEST(PricingTest, ScalesLinearlyWithTokens)
{
    const Pricing pricing = Pricing::load(pricingPath());

    const double small = pricing.cost(QStringLiteral("deepseek-flash"), Usage{1000, 200});
    EXPECT_DOUBLE_EQ(small, (1000 * 0.15 / 1000000 + 200 * 0.6 / 1000000) * 6.71);
}

TEST(PricingTest, ShowsTheVendorsCurrencyWhenNoDisplayBlockIsGiven)
{
    const TempPricing plain(R"({"currency":"EUR","unit":1000000,"models":{"m":{"input":1,"output":2}}})");
    const Pricing pricing = Pricing::load(plain.path);

    EXPECT_EQ(pricing.currency(), QStringLiteral("EUR"));
    EXPECT_DOUBLE_EQ(pricing.cost(QStringLiteral("m"), Usage{1000000, 1000000}), 3.0);
}

TEST(PricingTest, CostsAnUnlistedModelAtZeroRatherThanGuessing)
{
    const Pricing pricing = Pricing::load(pricingPath());

    EXPECT_DOUBLE_EQ(pricing.cost(QStringLiteral("no-such-model"), Usage{1000000, 1000000}), 0.0);
}

/// What the price list has to say is about the list, not about the call: a model it does not
/// carry is still not carried on the thousandth call. Saying so every time is what filled a 2 MB
/// log in a day -- the budget is re-evaluated twice a second, and each evaluation prices the whole
/// day's usage -- so the line read as noise instead of as the one thing to act on.
TEST(PricingTest, AModelTheListDoesNotCarryIsReportedOnceAndNotOnEveryCall)
{
    const Pricing pricing = Pricing::load(pricingPath());
    const LogSpy spy;

    for (int call = 0; call < 50; ++call)
        EXPECT_DOUBLE_EQ(pricing.cost(QStringLiteral("no-such-model"), Usage{1000000, 1000000}), 0.0);

    EXPECT_EQ(spy.count("no-such-model"), 1) << "the unlisted model was reported once per call";

    // A second unlisted model is its own news -- one line each, not one line in total.
    pricing.cost(QStringLiteral("another-unlisted"), Usage{1000000, 1000000});
    pricing.cost(QStringLiteral("another-unlisted"), Usage{1000000, 1000000});
    EXPECT_EQ(spy.count("another-unlisted"), 1);
    EXPECT_EQ(spy.count("no-such-model"), 1);

    // And a model the list does carry is not news at all.
    pricing.cost(QStringLiteral("deepseek-flash"), Usage{1000000, 1000000});
    EXPECT_EQ(spy.count("deepseek-flash"), 0);
}

TEST(PricingTest, RefusesABrokenPriceList)
{
    const TempPricing noCurrency(R"({"unit":1000000,"models":{"m":{"input":1,"output":2}}})");
    const TempPricing noModels(R"({"currency":"USD","unit":1000000,"models":{}})");
    const TempPricing notJson("not json");
    // A display block that cannot convert: a zero rate would show every cost as free.
    const TempPricing noRate(R"({"currency":"USD","display":{"currency":"CNY"},"unit":1000000,"models":{"m":{"input":1,"output":2}}})");
    const TempPricing zeroRate(R"({"currency":"USD","display":{"currency":"CNY","multiplier":0},"unit":1000000,"models":{"m":{"input":1,"output":2}}})");
    const TempPricing noCode(R"({"currency":"USD","display":{"multiplier":6.7},"unit":1000000,"models":{"m":{"input":1,"output":2}}})");

    EXPECT_THROW(Pricing::load(noCurrency.path), std::runtime_error);
    EXPECT_THROW(Pricing::load(noModels.path), std::runtime_error);
    EXPECT_THROW(Pricing::load(notJson.path), std::runtime_error);
    EXPECT_THROW(Pricing::load(noRate.path), std::runtime_error);
    EXPECT_THROW(Pricing::load(zeroRate.path), std::runtime_error);
    EXPECT_THROW(Pricing::load(noCode.path), std::runtime_error);
    EXPECT_THROW(Pricing::load(std::filesystem::path("does-not-exist.json")), std::runtime_error);
}
