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

/// @brief Kinds a notice carries. The router in Main.qml keys the action off the kind: every
///        notice but the crash one explains a word.
inline constexpr const char* kNoticeInfo  = "info";
inline constexpr const char* kNoticeError = "error";
/// A crash notice, raised by the facade at the launch after a crash. Its two actions act on the
/// dump artifacts, not on a word.
inline constexpr const char* kNoticeCrash = "crash";

/**
 * @brief The map a notice surface reads.
 * @param title           What the notice is about -- normally the selection it answers.
 * @param body            Reader-facing reason, already translated.
 * @param kind            kNoticeInfo, kNoticeError or kNoticeCrash.
 * @param action          Label for the one thing the reader can do about it, empty when there is none.
 * @param lemma           The word that action would explain, when the notice is about a word.
 * @param secondaryAction Label for the second thing, empty when there is none; the crash notice
 *                        is so far the only one that carries two.
 * @return {title, body, kind, action, lemma, secondaryAction}. An empty map is not a notice: it
 *         means there is none.
 */
inline QVariantMap noticePayload(const QString& title, const QString& body, const QString& kind, const QString& action = {}, const QString& lemma = {}, const QString& secondaryAction = {})
{
    return QVariantMap{{"title", title}, {"body", body}, {"kind", kind}, {"action", action}, {"lemma", lemma}, {"secondaryAction", secondaryAction}};
}

}
