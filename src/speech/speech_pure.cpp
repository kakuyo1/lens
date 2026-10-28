/**
 * @file speech_pure.cpp
 * @brief The pronunciation rules behind speech_pure.h.
 */

#include "speech/speech_pure.h"

#include <QStringList>

#include "core/crypto/hash.h"

namespace lens::speech {
namespace {

constexpr QStringView kAmerican = u"us";
constexpr QStringView kBritish  = u"uk";

} // namespace

Engine engineFrom(QString const& value)
{
    if (value == QLatin1String("online"))
        return Engine::Online;
    if (value == QLatin1String("local"))
        return Engine::Local;
    return Engine::System;
}

Fallback fallbackFrom(QString const& value)
{
    if (value == QLatin1String("local"))
        return Fallback::Local;
    if (value == QLatin1String("none"))
        return Fallback::None;
    return Fallback::Online;
}

QString engineName(Engine engine)
{
    switch (engine) {
        case Engine::Online:
            return QStringLiteral("online");
        case Engine::Local:
            return QStringLiteral("local");
        case Engine::System:
            break;
    }
    return QStringLiteral("system");
}

QString fallbackName(Fallback fallback)
{
    switch (fallback) {
        case Fallback::Local:
            return QStringLiteral("local");
        case Fallback::None:
            return QStringLiteral("none");
        case Fallback::Online:
            break;
    }
    return QStringLiteral("online");
}

QString accentFrom(QString const& value)
{
    return value == QLatin1String("uk") ? QString{kBritish} : QString{kAmerican};
}

QString onlineType(QString const& accent)
{
    // British is 1 and American is 2; the endpoint numbers them the other way round from the way
    // the settings list them, which is why this is a table rather than a comparison.
    return accent == QLatin1String("uk") ? QStringLiteral("1") : QStringLiteral("2");
}

std::optional<Engine> plan(Engine chosen, Fallback fallback, bool systemUsable, bool localUsable)
{
    switch (chosen) {
        case Engine::System:
            if (systemUsable)
                return Engine::System;
            switch (fallback) {
                case Fallback::Online:
                    return Engine::Online;
                case Fallback::Local:
                    return localUsable ? std::optional<Engine>{Engine::Local} : std::nullopt;
                case Fallback::None:
                    return std::nullopt;
            }
            return std::nullopt;
        case Engine::Online:
            return Engine::Online;
        case Engine::Local:
            return localUsable ? std::optional<Engine>{Engine::Local} : std::nullopt;
    }
    return std::nullopt;
}

bool isHeadword(QString const& word)
{
    if (word.isEmpty() or word.size() > 64)
        return false;
    for (const QChar character : word)
        if (character == QLatin1Char('\n') or character == QLatin1Char('\r') or character == QLatin1Char('\t'))
            return false;
    return true;
}

QString cacheKey(QString const& accent, QString const& word)
{
    const QByteArray material = (accent + QLatin1Char('|') + word).toUtf8();
    return QString::fromStdString(core::crypto::sha1Hex(std::string_view{material.constData(), std::size_t(material.size())}));
}

}
