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
