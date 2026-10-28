/**
 * @file consent_preview_test.cpp
 * @brief The confirmation card's image source holds one crop and releases it by request id.
 */
#include <gtest/gtest.h>

#include <QImage>
#include <QSize>

#include "app/consent_preview.h"

TEST(ConsentPreview, OneCropAtATimeAndOnlyUnderItsRequestId)
{
    lens::app::ConsentPreview preview;
    QImage                    red{10, 10, QImage::Format_RGB32};
    red.fill(Qt::red);

    preview.set(7, red);
    QSize size;
    EXPECT_EQ(preview.requestImage(QStringLiteral("7"), &size, {}).pixelColor(0, 0), QColor{Qt::red});
    EXPECT_EQ(size, (QSize{10, 10}));
    EXPECT_TRUE(preview.requestImage(QStringLiteral("6"), nullptr, {}).isNull());

    QImage blue{20, 20, QImage::Format_RGB32};
    blue.fill(Qt::blue);
    preview.set(8, blue);
    EXPECT_TRUE(preview.requestImage(QStringLiteral("7"), nullptr, {}).isNull()) << "a new consent replaces the old crop";
    EXPECT_EQ(preview.requestImage(QStringLiteral("8"), nullptr, {}).pixelColor(0, 0), QColor{Qt::blue});

    preview.clear(8);
    EXPECT_TRUE(preview.requestImage(QStringLiteral("8"), nullptr, {}).isNull());
}
