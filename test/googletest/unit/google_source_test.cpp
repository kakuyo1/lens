/**
 * @file google_source_test.cpp
 * @brief The Google recording source's address, without a network: the voice is chosen by the language
 *        tag, and the word is one query value and cannot add a parameter of its own.
 */

#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <gtest/gtest.h>

#include "speech/google_source.h"

namespace {

/// @return The decoded value of one query parameter.
QString queryValue(QUrl const& url, QString const& name)
{
    return QUrlQuery{url}.queryItemValue(name);
}

}

TEST(GoogleSource, TheLanguageTagPicksTheVoice)
{
    EXPECT_EQ(queryValue(lens::speech::GoogleSource::urlFor(QStringLiteral("hello"), QStringLiteral("us")), QStringLiteral("tl")),
              QStringLiteral("en-US"));
    EXPECT_EQ(queryValue(lens::speech::GoogleSource::urlFor(QStringLiteral("hello"), QStringLiteral("uk")), QStringLiteral("tl")),
              QStringLiteral("en-GB"));
}

TEST(GoogleSource, TheAddressIsTheFixedEndpoint)
{
    const QUrl url = lens::speech::GoogleSource::urlFor(QStringLiteral("hello"), QStringLiteral("us"));
    EXPECT_EQ(url.scheme(), QStringLiteral("https"));
    EXPECT_EQ(url.host(), QStringLiteral("translate.google.com"));
    EXPECT_EQ(url.path(), QStringLiteral("/translate_tts"));
}

TEST(GoogleSource, TheWordIsOneValueAndCannotAddAParameter)
{
    // A word with an ampersand in it must stay the word: it cannot switch the voice or add a field.
    const QUrl url = lens::speech::GoogleSource::urlFor(QStringLiteral("a&tl=fr"), QStringLiteral("uk"));
    EXPECT_EQ(queryValue(url, QStringLiteral("q")), QStringLiteral("a&tl=fr"));
    EXPECT_EQ(queryValue(url, QStringLiteral("tl")), QStringLiteral("en-GB"));
}
