/**
 * @file online_sources_test.cpp
 * @brief The choice from a reader's setting to an adapter, without a network: each named source
 *        picks a different adapter, and the adapters are kept, so switching back costs no
 *        construction.
 */

#include <QCoreApplication>

#include <gtest/gtest.h>

#include "scripted_manager.h"
#include "speech/online_sources.h"

namespace {

using lens::speech::OnlineSource;
using lens::speech::OnlineSources;
using lens::test::ScriptedManager;

/// One QCoreApplication for a case's duration: the sources are QObjects, and Qt allows one running
/// application at a time, so the case makes its own.
struct QtApplication {
    QtApplication()
        : application(argc, argv)
    {
    }

    int              argc     = 1;
    char             name[24] = "online_sources_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application;
};

} // namespace

TEST(OnlineSources, TheSettingPicksTheAdapterAndTheAdaptersAreKept)
{
    QtApplication   qt;
    ScriptedManager manager{500};
    OnlineSources   sources{&manager};

    auto& youdao = sources.pick(OnlineSource::Youdao);
    auto& google = sources.pick(OnlineSource::Google);

    // Two different adapters for two different settings...
    EXPECT_NE(&youdao, &google);
    // ...and the same one on the next press: a reader who switches back costs no construction.
    EXPECT_EQ(&sources.pick(OnlineSource::Youdao), &youdao);
    EXPECT_EQ(&sources.pick(OnlineSource::Google), &google);
}
