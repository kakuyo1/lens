/**
 * @file word_answer.cpp
 * @brief The field-table conversions declared in word_answer.h.
 */

#include "word_answer.h"

#include <QByteArray>

#include <string_view>
#include <utility>

#include "core/domain/filter_core.h"
#include "util/word_fields.h"

namespace lens::app::word_answer {
namespace {

using Answer = llm::Explanation;
using Cached = core::CachedSense;
using Sense  = llm::Sense;
using Stored = core::WordCache;

using word::Block;
using word::Scope;
using word::Slot;

/// @brief One answer row, and where its value lives in each of the two records.
struct AnswerRef {
    word::Field const* field                      = nullptr;
    QString Answer::* text                        = nullptr;
    QStringList Answer::* list                    = nullptr;
    bool Answer::* flag                           = nullptr;
    std::string Stored::*    storedText           = nullptr;
    std::vector<std::string> Stored::* storedList = nullptr;
    bool Stored::* storedFlag                     = nullptr;
};

/// @brief One sense row, and where its value lives in each of the two records.
struct SenseRef {
    word::Field const* field         = nullptr;
    QString Sense::* text            = nullptr;
    bool Sense::* flag               = nullptr;
    std::string Cached::* storedText = nullptr;
    bool Cached::* storedFlag        = nullptr;
};

/// The table rows the two records carry. The example has no stored pointer: it is an
/// `std::optional` whose absence and emptiness mean different things (docs/adr/0033), so its
/// store side is handled in the two conversions rather than through a member pointer.
constexpr AnswerRef kAnswerRefs[]{
    {.field = word::find(Scope::Answer, Slot::Ipa), .text = &Answer::ipa, .storedText = &Stored::ipa},
    {.field = word::find(Scope::Answer, Slot::Pos), .text = &Answer::pos, .storedText = &Stored::pos},
    {.field = word::find(Scope::Answer, Slot::En), .text = &Answer::en, .storedText = &Stored::en},
    {.field = word::find(Scope::Answer, Slot::Zh), .text = &Answer::zh, .storedText = &Stored::zh},
    {.field = word::find(Scope::Answer, Slot::Translation), .text = &Answer::translation, .storedText = &Stored::translation},
    {.field = word::find(Scope::Answer, Slot::Etymology), .text = &Answer::etymology, .storedText = &Stored::etymology},
    {.field = word::find(Scope::Answer, Slot::Example), .text = &Answer::example},
    {.field = word::find(Scope::Answer, Slot::Signpost), .text = &Answer::signpost, .storedText = &Stored::signpost},
    {.field = word::find(Scope::Answer, Slot::Collocations), .list = &Answer::collocations, .storedList = &Stored::collocations},
    {.field = word::find(Scope::Answer, Slot::Imageable), .flag = &Answer::imageable, .storedFlag = &Stored::imageable},
    {.field = word::find(Scope::Answer, Slot::ImageQuery), .text = &Answer::imageQuery, .storedText = &Stored::imageQuery},
};

constexpr SenseRef kSenseRefs[]{
    {.field = word::find(Scope::Sense, Slot::En), .text = &Sense::en, .storedText = &Cached::en},
    {.field = word::find(Scope::Sense, Slot::Zh), .text = &Sense::zh, .storedText = &Cached::zh},
    {.field = word::find(Scope::Sense, Slot::Translation), .text = &Sense::translation, .storedText = &Cached::translation},
    {.field = word::find(Scope::Sense, Slot::Imageable), .flag = &Sense::imageable, .storedFlag = &Cached::imageable},
    {.field = word::find(Scope::Sense, Slot::ImageQuery), .text = &Sense::imageQuery, .storedText = &Cached::imageQuery},
};

bool shown(Block const block, Switches const& switches)
{
    switch (block) {
        case Block::Ipa:
            return switches.ipa;
        case Block::Pos:
            return switches.pos;
        case Block::Signpost:
            return switches.signpost;
        case Block::Collocations:
            return switches.collocations;
        case Block::Pictures:
            return switches.pictures;
        case Block::Etymology:
            return switches.etymology;
        case Block::Example:
            return switches.example;
        case Block::None:
            return true;
    }
    return true;
}

QVariant valueOf(Answer const& answer, AnswerRef const& ref)
{
    if (ref.text)
        return answer.*ref.text;
    if (ref.list) {
        QVariantList values;
        for (auto const& value : answer.*ref.list)
            values.append(value);
        return values;
    }
    return answer.*ref.flag;
}

QVariant emptyOf(AnswerRef const& ref)
{
    if (ref.list)
        return QVariantList{};
    if (ref.flag)
        return false;
    return QString{};
}

QString keyOf(word::Field const& field)
{
    return QString::fromLatin1(field.key.data(), static_cast<int>(field.key.size()));
}

QString senseText(Sense const& sense, QString const& language)
{
    return language == QLatin1String("en")   ? sense.en
           : language == QLatin1String("zh") ? sense.zh
                                             : sense.translation;
}

}

QString answerText(llm::Explanation const& answer, QString const& language)
{
    return language == QLatin1String("en")   ? answer.en
           : language == QLatin1String("zh") ? answer.zh
                                             : answer.translation;
}

QVariantMap cardFields(llm::Explanation const& answer, QString const& language, Switches const& switches, int const level)
{
    QVariantMap payload;
    for (auto const& ref : kAnswerRefs) {
        auto const& field = *ref.field;
        if (field.block == Block::None or field.block == Block::Pictures)
            continue;
        const bool on = shown(field.block, switches);
        // The origin and the example are bought settings: off leaves the key absent, so the
        // card's own default decides. Every other block is drawn empty when its switch is off.
        if ((field.block == Block::Etymology or field.block == Block::Example) and not on)
            continue;
        payload.insert(keyOf(field), on ? valueOf(answer, ref) : emptyOf(ref));
    }
    // The links are an English reading: another language has no frequency band to judge against,
    // so its block carries no spans and the card draws the text plain.
    payload.insert(QStringLiteral("spans"), language == QLatin1String("en") ? spansOf(answer.en, level) : QVariantList{});
    return payload;
}

QVariantMap senseRow(llm::Sense const& sense, QString const& language, bool const pictures, int const level)
{
    const QString english = language == QLatin1String("en") ? sense.en : QString{};
    return {{"en", english},
            {"spans", spansOf(english, level)},
            {"text", language == QLatin1String("en") ? QString{} : senseText(sense, language)},
            {"imageable", pictures and sense.imageable},
            {"imageQuery", pictures ? sense.imageQuery : QString{}}};
}

QVariantMap chainEntry(llm::Explanation const& answer, QString const& language, Switches const& switches, int const level)
{
    const QString text = answerText(answer, language);
    QVariantMap   entry{{"title", answer.title},
                        {"text", text},
                        {"spans", language == QLatin1String("en") ? spansOf(text, level) : QVariantList{}}};
    for (auto const& ref : kAnswerRefs) {
        auto const& field = *ref.field;
        if (field.block != Block::Ipa and field.block != Block::Signpost and field.block != Block::Collocations)
            continue;
        const bool on = shown(field.block, switches);
        entry.insert(keyOf(field), on ? valueOf(answer, ref) : emptyOf(ref));
    }
    return entry;
}

QVariantList spansOf(QString const& text, int const level)
{
    // The core counts bytes; ChainText draws with UTF-16 indices, so each offset is converted on its own prefix.
    QByteArray const bytes  = text.toUtf8();
    const auto       cutoff = core::rankForLevel(level);
    QVariantList     spans;
    for (auto const& link : core::definitionLinks(std::string_view{bytes.constData(), static_cast<std::size_t>(bytes.size())}, cutoff)) {
        const auto start = QString::fromUtf8(bytes.constData(), static_cast<int>(link.start)).size();
        const auto end   = QString::fromUtf8(bytes.constData(), static_cast<int>(link.end)).size();
        spans.append(QVariantMap{{"start", start}, {"end", end}, {"lemma", QString::fromStdString(link.lemma)}});
    }
    return spans;
}

core::WordCache toCache(llm::Explanation const& answer, bool const exampleAsked)
{
    core::WordCache cached;
    for (auto const& ref : kAnswerRefs) {
        if (ref.field->triState) {
            if (exampleAsked)
                cached.example = (answer.*ref.text).toStdString();
            continue;
        }
        if (ref.text)
            cached.*ref.storedText = (answer.*ref.text).toStdString();
        else if (ref.list)
            for (auto const& value : answer.*ref.list)
                (cached.*ref.storedList).push_back(value.toStdString());
        else if (ref.flag)
            cached.*ref.storedFlag = answer.*ref.flag;
    }
    for (auto const& sense : answer.senses) {
        core::CachedSense parsed;
        for (auto const& ref : kSenseRefs) {
            if (ref.text)
                parsed.*ref.storedText = (sense.*ref.text).toStdString();
            else if (ref.flag)
                parsed.*ref.storedFlag = sense.*ref.flag;
        }
        cached.senses.push_back(std::move(parsed));
    }
    return cached;
}

llm::Explanation fromCache(core::WordCache const& cached, QString const& lemma)
{
    llm::Explanation answer;
    answer.title = lemma;
    for (auto const& ref : kAnswerRefs) {
        if (ref.field->triState) {
            if (cached.example)
                answer.*ref.text = QString::fromStdString(*cached.example);
            continue;
        }
        if (ref.text)
            answer.*ref.text = QString::fromStdString(cached.*ref.storedText);
        else if (ref.list)
            for (auto const& value : cached.*ref.storedList)
                (answer.*ref.list) << QString::fromStdString(value);
        else if (ref.flag)
            answer.*ref.flag = cached.*ref.storedFlag;
    }
    for (auto const& sense : cached.senses) {
        llm::Sense parsed;
        for (auto const& ref : kSenseRefs) {
            if (ref.text)
                parsed.*ref.text = QString::fromStdString(sense.*ref.storedText);
            else if (ref.flag)
                parsed.*ref.flag = sense.*ref.storedFlag;
        }
        answer.senses.push_back(std::move(parsed));
    }
    return answer;
}

}
