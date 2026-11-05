/** @file scan_pipeline.cpp
 * @brief The process whitelist match behind the hover lookup.
 */
#include "scan_pipeline.h"

#include <algorithm>

#include "util/text.h"

namespace lens::capture {
namespace {

std::string basename(std::string_view const path)
{
    auto const at = path.find_last_of("/\\");
    return util::asciiLower(at == std::string_view::npos ? path : path.substr(at + 1));
}

}

bool allowedProcess(std::string_view const process, std::vector<std::string> const& whitelist, bool const own)
{
    if (own or process.empty()) return false;
    auto const name = basename(process);
    return std::any_of(whitelist.begin(), whitelist.end(), [&](auto const& item) {
        return not item.empty() and basename(item) == name;
    });
}

}
