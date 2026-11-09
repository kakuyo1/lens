/**
 * @file upload_transaction_test.cpp
 * @brief The upload transaction's one silent path: a refusal with no transaction live.
 */

#include <QString>

#include <gtest/gtest.h>

#include "app/duties/upload_transaction.h"

namespace {

using lens::app::UploadStep;
using lens::app::UploadTransaction;
using lens::ocr::OnlineOcrConfig;

}

/// The executor can refuse a job after the transaction was cleared -- the consent clock or the
/// reader's cancel won the race. The refusal is then not the reader's to see, and must not read
/// a transaction that is no longer there.
TEST(UploadTransaction, ARefusalWithNoTransactionIsSilent)
{
    UploadTransaction transaction(nullptr, nullptr);
    ASSERT_FALSE(transaction.active());

    const UploadStep step = transaction.submitRefused();

    EXPECT_EQ(step.kind, UploadStep::Kind::None);
    EXPECT_FALSE(step.announced);
    EXPECT_FALSE(step.changed);
    EXPECT_EQ(transaction.phase(), QStringLiteral("idle"));
    EXPECT_FALSE(transaction.canRetry());
}

/// The phase lives in one place: the pending record's own stage. The settings projection still
/// reads the same vocabulary, and the retry window is the only stage that offers a retry.
TEST(UploadTransaction, APendingTransactionMovesThroughItsPhases)
{
    UploadTransaction transaction(nullptr, nullptr);
    const auto        now = std::chrono::steady_clock::now();

    const UploadStep begun = transaction.begin(QImage{1, 1, QImage::Format_ARGB32}, QPoint{}, OnlineOcrConfig{}, 7, now);
    EXPECT_EQ(begun.kind, UploadStep::Kind::Consent);
    EXPECT_EQ(transaction.phase(), QStringLiteral("awaiting-consent"));
    EXPECT_FALSE(transaction.canRetry());

    const UploadStep refused = transaction.submitRefused();
    EXPECT_EQ(refused.kind, UploadStep::Kind::Finished);
    EXPECT_EQ(transaction.phase(), QStringLiteral("awaiting-retry"));
    EXPECT_TRUE(transaction.canRetry());
    EXPECT_EQ(refused.retryWindow, UploadTransaction::kRetryWindow);

    const UploadStep expired = transaction.consentTimedOut();
    EXPECT_TRUE(expired.cleared);
    EXPECT_EQ(expired.outcome.status, QStringLiteral("retry-expired"));
    EXPECT_EQ(transaction.phase(), QStringLiteral("idle"));
    EXPECT_FALSE(transaction.canRetry());
    EXPECT_FALSE(transaction.active());
}
