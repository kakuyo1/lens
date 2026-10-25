/**
 * @file picture_pure.cpp
 * @brief The network-free half of the picture lookup, behind picture_pure.h.
 */

#include "pictures/picture_pure.h"

#include <QCryptographicHash>
#include <QBuffer>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace lens::pictures {
namespace {

/// @brief The licences a picture may carry to be shown.
///
/// A picture whose licence is anything else -- GFDL-only, fair use, unknown, all rights reserved --
/// is dropped rather than displayed with an attribution the reader cannot act on.
bool licenseAllowed(QString const& raw)
{
    const QString name = plainText(raw).toLower();
    if (name.isEmpty())
        return false;
    static const QStringList allowed{
        QStringLiteral("public domain"), QStringLiteral("cc0"), QStringLiteral("cc by 2.0"), QStringLiteral("cc by 3.0"), QStringLiteral("cc by 4.0"), QStringLiteral("cc by-sa 2.0"), QStringLiteral("cc by-sa 3.0"), QStringLiteral("cc by-sa 4.0")};
    for (auto const& one : allowed) {
        if (name == one)
            return true;
    }
    // Commons spells the public-domain dedication a few ways: "CC0 1.0", "Public domain (PD-US)".
    return name.startsWith(QStringLiteral("cc0")) or name.startsWith(QStringLiteral("public domain"));
}

/// @brief Whether a title or description names something that is not a photograph.
///
/// A heuristic: Commons answers with diagrams, maps, coats of arms and the like, and only reading
/// the picture itself could tell those from photographs. It drops what it recognises and lets the
/// rest through.
bool looksLikeNonPhoto(QString const& text)
{
    if (text.isEmpty())
        return false;
    static const QStringList markers{
        QStringLiteral("diagram"), QStringLiteral("map of"), QStringLiteral("logo"), QStringLiteral("chart"), QStringLiteral("graph of"), QStringLiteral("schematic"), QStringLiteral("poster"), QStringLiteral("coat of arms"), QStringLiteral("flag of"), QStringLiteral("seal of"), QStringLiteral("signature"), QStringLiteral("clipart"), QStringLiteral("drawing of"), QStringLiteral("illustration of")};
    const QString lowered = text.toLower();
    for (auto const& marker : markers) {
        if (lowered.contains(marker))
            return true;
    }
    return false;
}

/// @brief Whether a caption is written in a script the card can read as English.
///
/// A description in another script is dropped for the file name instead: the card's captions are
/// English, and a fall-back is preferred to a machine translation.
bool looksEnglish(QString const& text)
{
    for (const QChar c : text) {
        const ushort code = c.unicode();
        if ((code >= 0x0400 and code <= 0x04FF) or // Cyrillic
            (code >= 0x0590 and code <= 0x05FF) or // Hebrew
            (code >= 0x0600 and code <= 0x06FF) or // Arabic
            (code >= 0x0E00 and code <= 0x0E7F) or // Thai
            (code >= 0x3040 and code <= 0x30FF) or // Hiragana and Katakana
            (code >= 0x3400 and code <= 0x4DBF) or // CJK extension A
            (code >= 0x4E00 and code <= 0x9FFF) or // CJK unified
            (code >= 0xAC00 and code <= 0xD7AF))   // Hangul
            return false;
    }
    return true;
}

/// @return A Commons title ("File:Garden trowel.jpg") as caption words: prefix, extension and
///         separators gone.
QString titleCaption(QString const& title)
{
    QString name = title;
    if (name.startsWith(QStringLiteral("File:"), Qt::CaseInsensitive))
        name = name.mid(5);
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0)
        name = name.left(dot);
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    name.replace(QLatin1Char('-'), QLatin1Char(' '));
    return name.simplified();
}

} // namespace

QString normalizedQuery(QString const& query)
{
    return query.simplified().toLower();
}

QString cacheKey(QString const& normalizedQuery, int width)
{
    const QByteArray material = QByteArrayLiteral("commons\n") + normalizedQuery.toUtf8() + '\n' + QByteArray::number(width);
    return QString::fromLatin1(QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex());
}

QString imageSearchUrl(QString const& query)
{
    return QStringLiteral("https://www.bing.com/images/search?q=%1&setlang=en")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(query.trimmed())));
}

bool isAllowedThumbnailHost(QUrl const& url)
{
    const QString host = url.host().toLower();
    return url.scheme() == QLatin1String("https") and
           (host == QStringLiteral("upload.wikimedia.org") or host == QStringLiteral("thumb.wikimedia.org"));
}

bool decodesImage(QByteArray const& bytes)
{
    if (bytes.isEmpty())
        return false;
    QBuffer buffer;
    buffer.setData(bytes);
    if (not buffer.open(QIODevice::ReadOnly))
        return false;
    QImageReader reader(&buffer);
    // The size is enough to prove the header parses, and the full decode proves the body arrived:
    // a truncated JPEG has a valid header and no picture in it.
    return reader.canRead() and not reader.read().isNull();
}

QString plainText(QString const& value)
{
    static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
    QString                         text = value;
    text.remove(tag);
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    return text.simplified();
}

QVector<Picture> parseCommonsSearch(QByteArray const& body)
{
    const auto document = QJsonDocument::fromJson(body);
    if (not document.isObject())
        return {};
    const auto pages = document.object().value(QStringLiteral("query")).toObject().value(QStringLiteral("pages")).toArray();
    if (pages.isEmpty())
        return {};

    struct Candidate {
        int     index = 0;
        Picture picture;
    };
    QVector<Candidate> candidates;

    for (auto const& value : pages) {
        const auto page = value.toObject();
        // Namespace 6 is File:; anything else is not a file this may show. Only checked when the
        // answer states it, so a body that leaves it out is read on its other fields.
        if (page.contains(QStringLiteral("ns")) and page.value(QStringLiteral("ns")).toInt() != 6)
            continue;
        const auto info = page.value(QStringLiteral("imageinfo")).toArray();
        if (info.isEmpty())
            continue;
        const auto image = info.at(0).toObject();

        const QString mime = image.value(QStringLiteral("mime")).toString().toLower();
        if (mime != QStringLiteral("image/jpeg") and mime != QStringLiteral("image/webp"))
            continue;

        const int width  = image.value(QStringLiteral("width")).toInt();
        const int height = image.value(QStringLiteral("height")).toInt();
        // Landscape photographs only: a 4:3 cell that crops a tall picture crops its subject with it.
        if (width < 640 or height < 480 or width < static_cast<int>(height * 1.2))
            continue;

        const QUrl pageUrl{image.value(QStringLiteral("descriptionurl")).toString()};
        const QUrl thumbUrl{image.value(QStringLiteral("thumburl")).toString()};
        if (not pageUrl.isValid() or pageUrl.host().isEmpty() or not thumbUrl.isValid() or thumbUrl.host().isEmpty())
            continue;

        const auto metadata = image.value(QStringLiteral("extmetadata")).toObject();
        const auto field    = [&metadata](QString const& key) {
            return plainText(metadata.value(key).toObject().value(QStringLiteral("value")).toString());
        };

        const QString license = field(QStringLiteral("LicenseShortName"));
        if (not licenseAllowed(license))
            continue;

        const QString title       = page.value(QStringLiteral("title")).toString();
        const QString description = field(QStringLiteral("ImageDescription"));
        if (looksLikeNonPhoto(title) or looksLikeNonPhoto(description))
            continue;

        Picture picture;
        picture.thumbnail    = thumbUrl;
        picture.page         = pageUrl;
        picture.source       = QStringLiteral("Wikimedia Commons");
        picture.license      = license;
        picture.licenseUrl   = field(QStringLiteral("LicenseUrl"));
        const QString artist = field(QStringLiteral("Artist"));
        picture.author       = artist.isEmpty() ? field(QStringLiteral("Credit")) : artist;
        if (picture.author.isEmpty())
            picture.author = QStringLiteral("Unknown author");
        // The source's own description when it is in English, and the file name otherwise; the
        // query itself is the caller's last resort, since it is what the search already ran with.
        picture.caption = (not description.isEmpty() and looksEnglish(description)) ? description : titleCaption(title);
        candidates.push_back({page.value(QStringLiteral("index")).toInt(), picture});
    }

    // The API answers in search order and `index` restates it; the order here is the order the card
    // draws, so it is kept, with a stable sort leaving a page that named no index where it arrived.
    std::stable_sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b) {
        return a.index < b.index;
    });

    QVector<Picture> out;
    for (auto const& candidate : candidates) {
        if (out.size() >= 3)
            break;
        out.push_back(candidate.picture);
    }
    return out;
}

}