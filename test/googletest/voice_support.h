#pragma once

#include <QByteArray>
#include <QString>

#include "speech/voice_output.h"

/**
 * @file voice_support.h
 * @brief The pronunciation output a suite lends AppController: every verb is the silent answer.
 *
 * The controller takes the machine's voice engines and player from its composition root
 * (docs/adr/0183), and the adapter is compiled into the executable alone, so a test target cannot
 * construct it. What a case does not assert on -- sound -- it gets from here, which is also what a
 * machine with no voice pack answers. The press path itself is exercised by speech_duty_test over
 * this seam.
 */

namespace lens::test {

class SilentVoiceOutput final : public lens::speech::VoiceOutput {
public:
    QString systemVoice(QString const&) override
    {
        return {};
    }
    bool installed(QString const&) override
    {
        return false;
    }
    bool say(QString const&, QString const&) override
    {
        return false;
    }
    bool play(QByteArray const&) override
    {
        return false;
    }
    void speakLocal(QString const&, QString const&, QString const&, LocalSink) override
    {
    }
    void stop() override
    {
    }
    void releaseLocalVoice(QString const&) override
    {
    }
};

}
