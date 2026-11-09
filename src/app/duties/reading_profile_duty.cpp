/**
 * @file reading_profile_duty.cpp
 * @brief The weekly reading profile's counts, interpretation and story, for reading_profile_duty.h.
 */

#include "reading_profile_duty.h"

#include <utility>

#include "app/settings_access.h"
#include "core/document/reading_profile.h"
#include "core/document/story.h"
#include "core/domain/settings_table.h"
#include "util/log.h"
#include "util/text.h"

namespace lens::app {

ReadingProfileDuty::ReadingProfileDuty(StorageDuty& storage, llm::LlmClient& llm, CostDuty& cost, DateProvider dateProvider, QObject* parent)
    : QObject(parent),
      storage_(storage),
      llm_(llm),
      cost_(cost),
      dateProvider_(std::move(dateProvider))
{
    // Every pop and every answer moves the counts the panel shows, and CostDuty is where those are announced. The
    // budget moves whether a story can be asked for, so the story hears it too.
    connect(&cost_, &CostDuty::statsChanged, this, &ReadingProfileDuty::factsChanged);
    connect(&cost_, &CostDuty::statsChanged, this, &ReadingProfileDuty::storyChanged);
    connect(&llm_, &llm::LlmClient::profileAnswered, this, &ReadingProfileDuty::onAnswered);
    connect(&llm_, &llm::LlmClient::storyAnswered, this, &ReadingProfileDuty::onStoryAnswered);
}

core::ReadingProfile ReadingProfileDuty::weekProfile() const
{
    const std::string fromDate = weekStart().toString(Qt::ISODate).toStdString();
    const std::string toDate   = dateProvider_().toString(Qt::ISODate).toStdString();

    const core::KnownStore& known        = storage_.knownStore();
    const auto              partOfSpeech = [&known](const std::string& lemma) {
        const auto cached = known.cacheGet(lemma);
        return cached ? cached->pos : std::string{};
    };
    // The profile's new/known split is about learning, not about the verdict's strength: a
    // mastered word is as little a new word as a known one (docs/adr/0159).
    std::unordered_set<std::string> notNew = known.known();
    notNew.insert(known.mastered().begin(), known.mastered().end());
    return core::buildReadingProfile(storage_.statsStore().history(), notNew, partOfSpeech, fromDate, toDate, storage_.statsStore().daily());
}

QVariantMap ReadingProfileDuty::facts() const
{
    const QDate       from     = weekStart();
    const QDate       today    = dateProvider_();
    const std::string fromDate = from.toString(Qt::ISODate).toStdString();
    const std::string toDate   = today.toString(Qt::ISODate).toStdString();

    const core::ReadingProfile profile = weekProfile();

    QVariantList parts;
    for (const auto& [part, count] : profile.partsOfSpeech)
        parts.append(QVariantMap{{"part", QString::fromStdString(part)}, {"count", count}});

    QVariantList hours;
    for (const int count : profile.hours)
        hours.append(count);

    QVariantList top;
    for (const core::ProfileWord& word : profile.top)
        top.append(QVariantMap{{"lemma", QString::fromStdString(word.lemma)}, {"lookups", word.lookups}});

    return QVariantMap{{"from", QString::fromStdString(fromDate)},
                       {"to", QString::fromStdString(toDate)},
                       {"pops", profile.pops},
                       {"words", profile.words},
                       {"fresh", profile.fresh},
                       {"freshPercent", core::sharePercent(profile.fresh, profile.words)},
                       {"repeated", profile.repeated},
                       {"repeatedPercent", core::sharePercent(profile.repeated, profile.words)},
                       {"motherTonguePercent", core::sharePercent(profile.motherTonguePops, profile.explainedPops)},
                       {"peeks", profile.peeks},
                       {"uncached", profile.uncached},
                       {"partsOfSpeech", parts},
                       {"hours", hours},
                       {"top", top},
                       {"summary", QString::fromStdString(core::readingProfileSummary(profile, fromDate, toDate))}};
}

void ReadingProfileDuty::interpret()
{
    const QVariantMap now = facts();
    if (interpreting_ || storyWriting_ || now.value("pops").toInt() == 0 || !cost_.canRequest())
        return;

    interpreting_    = true;
    interpretedWeek_ = weekStart().toString(Qt::ISODate);
    points_.clear();
    failure_.clear();
    emit interpretationChanged();
    // The summary is the only text this request carries; the interface language picks the closing
    // line, so the observations read in the language of the panel that shows them.
    const QString language = settings::read(storage_.section(), *core::settings::find("uiLanguage")).toString();
    llm_.requestProfile(now.value("summary").toString(), language);
}

void ReadingProfileDuty::tellStory()
{
    const QString week = weekStart().toString(Qt::ISODate);
    if (storyWriting_ || !storyAvailable() || (storyWeek_ == week && !story_.text.isEmpty()))
        return;

    // The words are drawn here, from the week's own lemmas, so the request carries only what the story is written from.
    const std::vector<std::string> lemmas = core::pickStoryWords(weekProfile().lemmas, rng_);
    QStringList                    listed;
    for (const std::string& lemma : lemmas)
        listed.append(QString::fromStdString(lemma));

    storyWriting_ = true;
    storyWeek_    = week;
    story_        = {};
    storyFailure_.clear();
    emit storyChanged();
    llm_.requestStory(listed);
}

bool ReadingProfileDuty::interpreting() const
{
    return interpreting_;
}

QVariantList ReadingProfileDuty::interpretation() const
{
    return interpretedWeek_ == weekStart().toString(Qt::ISODate) ? points_ : QVariantList{};
}

QString ReadingProfileDuty::interpretFailure() const
{
    return interpretedWeek_ == weekStart().toString(Qt::ISODate) ? failure_ : QString{};
}

QString ReadingProfileDuty::story() const
{
    return storyWeek_ == weekStart().toString(Qt::ISODate) ? story_.text : QString{};
}

QStringList ReadingProfileDuty::storyWords() const
{
    return storyWeek_ == weekStart().toString(Qt::ISODate) ? story_.words : QStringList{};
}

bool ReadingProfileDuty::storyWriting() const
{
    return storyWriting_;
}

QString ReadingProfileDuty::storyFailure() const
{
    return storyWeek_ == weekStart().toString(Qt::ISODate) ? storyFailure_ : QString{};
}

bool ReadingProfileDuty::storyAvailable() const
{
    return !storyWriting_ && !interpreting_ && weekProfile().pops > 0 && cost_.canRequest();
}

QDate ReadingProfileDuty::weekStart() const
{
    const QDate today = dateProvider_();
    return today.addDays(1 - today.dayOfWeek());
}

void ReadingProfileDuty::onAnswered(QVector<llm::ProfileLine> points, llm::Usage usage, QString failure)
{
    interpreting_ = false;
    cost_.recordUsage(util::localMinuteNow(), usage);
    storage_.save();
    points_.clear();
    for (const llm::ProfileLine& point : points)
        points_.append(QVariantMap{{"text", point.text}, {"fact", point.fact}});
    failure_ = std::move(failure);
    emit interpretationChanged();
}

void ReadingProfileDuty::onStoryAnswered(llm::StoryAnswer answer, llm::Usage usage, QString failure)
{
    storyWriting_ = false;
    cost_.recordUsage(util::localMinuteNow(), usage);
    storage_.save();
    story_ = std::move(answer);
    // The story is never logged, only its length: the prompt asks for 100 to 300 words, and a longer answer is cut to the panel's limit.
    if (!story_.text.isEmpty()) {
        const std::string text  = story_.text.toStdString();
        const std::size_t words = core::countWords(text);
        if (words < 100 || words > 300)
            LENS_INFO("the story came back at {} word(s), outside the 100 to 300 asked for", words);
        story_.text = QString::fromStdString(core::truncateWords(text, core::kStoryTextMaxWords));
    }
    storyFailure_ = std::move(failure);
    emit storyChanged();
}

}
