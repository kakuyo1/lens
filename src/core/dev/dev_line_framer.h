#pragma once

/**
 * @file dev_line_framer.h
 * @brief Turns the bytes a developer pipe receives into whole lines, for the command grammar in dev_command.h
 *        (docs/adr/0068).
 *
 * Pure: no Qt and no socket. The pipe reads bytes and hands them here, and a client that sends a line past the cap
 * is cut off by the pipe.
 */

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace lens::core::dev {

/// Accumulates the bytes of one client's stream and yields the lines they complete.
struct LineFramer {
    /// The longest line accepted, in bytes before its line break, carriage return included. A client that sends more
    /// is cut off, so a local process cannot make the application hold an unbounded line in memory.
    static constexpr std::size_t kMaxLineBytes = 4096;

    /// What one feed() call produced.
    struct Output {
        /// The lines the bytes completed, without their line breaks.
        std::vector<std::string> lines;
        /// True once a line passed kMaxLineBytes. The stream is over, and the caller drops the client.
        bool overflow{false};
    };

    /// @param bytes The next bytes from the client, in arrival order.
    /// @return The lines @p bytes completed, and whether a line passed kMaxLineBytes.
    Output feed(std::string_view bytes);

private:
    /// The line still open: bytes received since the last line break.
    std::string partial_;
};

} // namespace lens::core::dev
