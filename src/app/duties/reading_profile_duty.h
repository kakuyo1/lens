#pragma once

#include <QDate>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <random>

#include "app/duties/cost_duty.h"
#include "app/duties/storage_duty.h"
#include "llm/llm_client.h"

namespace lens::core {
struct ReadingProfile;
}

/**
 * @file reading_profile_duty.h
 * @brief The weekly reading profile: this week's counts read from the local history, the one-shot interpretation the
 *        reader asks for (UI-SPEC.md 4.7b, docs/adr/0150), and the story written from the week's words (docs/adr/0152).
 */

namespace lens::app {

class ReadingProfileDuty final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.readingProfileDuty")
public:
    using DateProvider = std::function<QDate()>;

    /**
     * @brief Bind the profile to the statistics, the cache and the explanation client.
     * @param storage      Owns the history, the known set, the cache and the save.
     * @param llm          Client the interpretation and the story are asked through.
     * @param cost         The budget gate, and where the answers' tokens are booked.
     * @param dateProvider Today's local date. The window runs from its Monday to it, both inclusive.
     */
    ReadingProfileDuty(StorageDuty& storage, llm::LlmClient& llm, CostDuty& cost, DateProvider dateProvider = [] { return QDate::currentDate(); }, QObject* parent = nullptr);

    /// @return This week's counts, shares, mix and top words, read from the local history. No request.
    QVariantMap facts() const;

    /// @brief Ask for the interpretation of this week's profile.
    /// @note One request at a time. Nothing is sent for a week with no lookups or while the daily budget is spent.
    Q_INVOKABLE void interpret();

    /// @return Whether an interpretation request is in flight.
    bool interpreting() const;
    /// @return The points the current week's interpretation gave, each a map of "text" and "fact"; empty when none was asked for this week.
    QVariantList interpretation() const;
    /// @return The reader's reason the current week's interpretation failed, empty otherwise.
    QString interpretFailure() const;

    /// @brief Ask for the story written from this week's words, unless this week's story is already written or on its way.
    Q_INVOKABLE void tellStory();
    /// @return The story for this week, empty until it is written.
    QString story() const;
    /// @return The listed words the story uses, in the order the story gave them.
    QStringList storyWords() const;
    /// @return Whether the story request is in flight.
    bool storyWriting() const;
    /// @return The reader's reason the story failed, empty otherwise.
    QString storyFailure() const;
    /// @return Whether a story can be asked for now: the week has lookups, nothing is in flight, and the daily budget allows a request.
    bool storyAvailable() const;

    Q_PROPERTY(QVariantMap facts READ facts NOTIFY factsChanged)
    Q_PROPERTY(bool interpreting READ interpreting NOTIFY interpretationChanged)
    Q_PROPERTY(QVariantList interpretation READ interpretation NOTIFY interpretationChanged)
    Q_PROPERTY(QString interpretFailure READ interpretFailure NOTIFY interpretationChanged)
    Q_PROPERTY(QString story READ story NOTIFY storyChanged)
    Q_PROPERTY(QStringList storyWords READ storyWords NOTIFY storyChanged)
    Q_PROPERTY(bool storyWriting READ storyWriting NOTIFY storyChanged)
    Q_PROPERTY(QString storyFailure READ storyFailure NOTIFY storyChanged)
    Q_PROPERTY(bool storyAvailable READ storyAvailable NOTIFY storyChanged)

signals:
    /// @brief The statistics the counts are read from changed.
    void factsChanged();
    /// @brief The interpretation went up or came back.
    void interpretationChanged();
    /// @brief The story went up or came back, or the budget or the counts that decide whether one can be asked for moved.
    void storyChanged();

private:
    /// @return Monday of the week the date provider's day falls in.
    QDate weekStart() const;
    /// @return This week's profile, counted from the history. Nothing is cached.
    core::ReadingProfile weekProfile() const;
    /// @brief Keep the answer for the week the request was made in, and book its tokens.
    void onAnswered(QVector<llm::ProfileLine> points, llm::Usage usage, QString failure);
    /// @brief Keep the story for the week it was asked in, and book its tokens. The text is cut to the panel's limit here.
    void onStoryAnswered(llm::StoryAnswer answer, llm::Usage usage, QString failure);

    StorageDuty&    storage_;
    llm::LlmClient& llm_;
    CostDuty&       cost_;
    DateProvider    dateProvider_;
    bool            interpreting_ = false;
    QString         interpretedWeek_; ///< The Monday the last request was made for, "YYYY-MM-DD".
    QVariantList    points_;
    QString         failure_;

    bool             storyWriting_ = false;
    QString          storyWeek_; ///< The Monday the story was asked for, "YYYY-MM-DD".
    llm::StoryAnswer story_;     ///< The week's story and the listed words it uses; empty until it is written.
    QString          storyFailure_;
    std::mt19937     rng_{std::random_device{}()};
};

}
