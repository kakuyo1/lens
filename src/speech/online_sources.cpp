/**
 * @file online_sources.cpp
 * @brief The choice from a reader's setting to an adapter, behind online_sources.h.
 */

#include "speech/online_sources.h"

#include <QNetworkAccessManager>

namespace lens::speech {

OnlineSources::OnlineSources(QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent),
      manager_(manager != nullptr ? manager : new QNetworkAccessManager(this)),
      youdao_(manager_),
      google_(manager_)
{
}

PronunciationSource& OnlineSources::pick(OnlineSource source)
{
    switch (source) {
        case OnlineSource::Google:
            return google_;
        case OnlineSource::Youdao:
            break;
    }
    return youdao_;
}

}
