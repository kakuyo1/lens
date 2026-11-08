/**
 * @file hover_flow.cpp
 * @brief The hover rhythm, moved verbatim from the capture duty's hover block (docs/adr/0165).
 */

#include "hover_flow.h"

#include "util/log.h"

namespace lens::app {

bool HoverFlow::toggle()
{
    mode_  = not mode_;
    dwell_ = core::HoverDwell{};
    // A read the window never answers must not hold the switch closed: a late answer is checked against the mode anyway.
    busy_ = false;
    LENS_INFO("hover lookup {}", mode_ ? "on" : "off");
    return mode_;
}

HoverFlow::Step HoverFlow::tick(Facts const& facts, qint64 const nowMs)
{
    if (busy_ or facts.budgetPaused or not dwell_.update(nowMs, facts.pointer.x(), facts.pointer.y())) return {};
    // The read is a few round trips into the window under the pointer, so it runs off the main thread.
    busy_ = true;
    Step step;
    step.read      = true;
    step.at        = facts.pointer;
    step.whitelist = facts.whitelist;
    return step;
}

HoverFlow::Step HoverFlow::wordRead(std::optional<HoverWord> word, QPoint const pointerNow)
{
    busy_ = false;
    Step step;
    // The pointer may have left the word while the read was in flight; a bubble for a word it has left is noise.
    if (not word or not mode_ or not word->box.contains(pointerNow)) return step;
    auto const text = core::hoverWordFrom(word->text.toStdString());
    if (text.empty()) return step;
    step.asked  = true;
    step.text   = QString::fromStdString(text);
    step.anchor = QPoint{word->box.left(), word->box.bottom()};
    return step;
}

}
