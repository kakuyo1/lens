#pragma once

#include <QObject>

#include "speech/google_source.h"
#include "speech/pronunciation_source.h"
#include "speech/speech_pure.h"
#include "speech/youdao_source.h"

class QNetworkAccessManager;

/**
 * @file online_sources.h
 * @brief The online recording sources, one adapter each, and the one a reader's choice picks (docs/adr/0138).
 *
 * The duty asks this for a source per press rather than holding one: the choice is the reader's setting, and
 * a reader who changes it must hear the new source on the next word. Every adapter is built once and kept,
 * so switching back costs no construction. An adapter that is not in OnlineSource cannot be picked, which is
 * how a source is added: a new value there, a new adapter file, and a branch here.
 */

namespace lens::speech {

class OnlineSources final : public QObject {
    Q_OBJECT
public:
    /// @param manager The network manager every adapter posts through, or nullptr to own one.
    /// @param parent  The Qt parent, for the manager this object owns.
    explicit OnlineSources(QNetworkAccessManager* manager = nullptr, QObject* parent = nullptr);

    /// @return The adapter for @p source. Owned here; the reference lives as long as this object.
    PronunciationSource& pick(OnlineSource source);

private:
    QNetworkAccessManager* manager_;
    YoudaoSource           youdao_;
    GoogleSource           google_;
};

}
