#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file file_transport.h
 * @brief The file-level network the event channel asks for its month files (docs/adr/0181).
 *
 * The round's Transport (sync_round.h) carries the one cloud copy the sync document is; this
 * carries the event files that live beside it: list them, write one, delete one, read one back.
 * The provider's dialect -- a gist's file map and raw addresses, a WebDAV collection's paths --
 * stays behind the seam, as does its credentials, read fresh each call so a settings edit takes
 * effect on the next request.
 */

namespace lens::core::sync {

/// The collection WebDAV backends keep the cloud copy and its event files in (docs/adr/0157).
inline constexpr std::string_view kWebdavCollection = "Lens";

/// @brief One request to carry a month file's operation.
struct FileRequest {
    /// @brief Which call to make.
    enum class Kind : int {
        List,   ///< The event file names the cloud copy carries.
        Put,    ///< Write @c name with @c content, overwriting any earlier version.
        Delete, ///< Remove @c name.
        Get     ///< Read @c name's text.
    };

    Kind        kind = Kind::List;
    std::string name;    ///< Put, Delete, Get: the event file name.
    std::string content; ///< Put: the sealed text.
};

/// @brief The answer to one file request.
struct FileAnswer {
    /// @brief Why a call did not answer with what was asked for.
    enum class Failure : int {
        None,
        Unauthorised, ///< The credentials are refused.
        NotFound,     ///< The cloud copy the files live in is not there.
        Proxy,        ///< The connection never got a byte.
        Generic,      ///< Anything else, or a body that was not what was asked for.
        Unconfigured, ///< This device's credentials for the provider do not stand yet.
    };

    Failure                  failure = Failure::None;
    std::vector<std::string> names;   ///< List: every file name the cloud copy carries.
    std::string              content; ///< Get: the file's text; empty when the file is absent.
};

/**
 * @brief The network a file channel asks; the application's provider transports are the adapters.
 *
 * A reply is delivered exactly once and may arrive after send() returns. The queue beneath the
 * interface delivers one request at a time with an identity, as the round's Transport does
 * (docs/adr/0170). `Unconfigured` answers name no transport fault: the channel waits for the
 * reader to finish signing in, and the caller ends its run without a failure to report.
 */
class FileTransport {
public:
    virtual ~FileTransport() = default;

    using Reply = std::function<void(const FileAnswer&)>;
    /// @brief Carry @p request and call @p reply once, when the answer is in.
    virtual void send(const FileRequest& request, Reply reply) = 0;
};

}
