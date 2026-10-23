#pragma once

#include <QObject>

class QQmlEngine;

/**
 * @brief The singletons the surfaces bind to, handed over before any case's QML is loaded.
 *
 * quick_test_main_with_setup() calls qmlEngineAvailable() once per test file, after the
 * engine exists and before its root object is built: the only window in which a
 * QML_SINGLETON can be provided. Without it every read off Controller is a TypeError on a
 * null, and AppController::create() asserts, so a Debug build dies instead of failing the
 * case.
 *
 * The body is compiled out when LENS_QTEST_SINGLETONS is undefined; that is how the
 * components target runs with no settings document and no price list behind it. See TEST.md
 * section 2.
 */
class LensTestSetup : public QObject {
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine* engine);
};
