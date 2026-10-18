/**
 * @file dev_line_framer.cpp
 * @brief The line framing for dev_line_framer.h.
 */

#include "core/dev_line_framer.h"

#include <utility>

namespace lens::core::dev {

LineFramer::Output LineFramer::feed(std::string_view bytes)
{
    Output output;
    while (true) {
        auto const breakAt = bytes.find('\n');
        partial_.append(bytes.substr(0, breakAt));
        if (partial_.size() > kMaxLineBytes) {
            output.overflow = true;
            partial_.clear();
            return output;
        }
        if (breakAt == std::string_view::npos)
            return output;

        if (not partial_.empty() and partial_.back() == '\r')
            partial_.pop_back();
        output.lines.push_back(std::move(partial_));
        partial_.clear();
        bytes.remove_prefix(breakAt + 1);
    }
}

} // namespace lens::core::dev
