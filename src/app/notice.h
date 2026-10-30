#pragma once

#include <QString>
#include <QVariantMap>

/**
 * @file notice.h
 * @brief The payload a notice surface binds to: the reason a pop had nothing to explain.
 *
 * A notice is not an explanation and carries no verdict; it exists so the bubble is no longer
 * the error reporter. A request that failed, and a selection with no word to look up, both
 * arrive here, and the surface that draws a notice can be built on its own.
 *
 * The shape lives here rather than in AppController so that the surface, the controller and
 * the offline test all read one definition of it.
 */

namespace lens::app {

/// @brief Kinds a notice carries. Nothing depends on a particular rendering of them yet; the
///        surface that arrives later decides whether it distinguishes them.
inline constexpr const char* kNoticeInfo  = "info";
inline constexpr const char* kNoticeError = "error";

/// @brief The intent of an action that is not an explanation: the reader is sent to where an API key is typed.
inline constexpr const char* kNoticeIntentApiKey = "apiKey";

/**
 * @brief The map a notice surface reads.
 * @param title  What the notice is about -- normally the selection it answers.
 * @param body   Reader-facing reason, already translated.
 * @param kind   kNoticeInfo or kNoticeError.
 * @param action Label for the one thing the reader can do about it, empty when there is none.
 * @param lemma  The word that action would explain, when the notice is about a word.
 * @param intent What the action does when it is not an explanation, kNoticeIntentApiKey or empty for an explanation.
 * @return {title, body, kind, action, lemma, intent}. An empty map is not a notice: it means there is none.
 */
inline QVariantMap noticePayload(const QString& title, const QString& body, const QString& kind, const QString& action = {}, const QString& lemma = {}, const QString& intent = {})
{
    return QVariantMap{{"title", title}, {"body", body}, {"kind", kind}, {"action", action}, {"lemma", lemma}, {"intent", intent}};
}

}
