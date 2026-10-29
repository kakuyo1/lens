/** @file ocr_json.h
 * @brief The strict little JSON readers the online adapters share.
 *
 * Each service returns numbers in its own mood (integers, strings holding integers, floats), and
 * each adapter must refuse a field it cannot read rather than guess. These three readers say the
 * same thing in one place for the adapters that came after the first two.
 */
#pragma once

#include <QString>

#include <nlohmann/json.hpp>

#include <charconv>
#include <cmath>

namespace lens::ocr::detail {

/// @return Whether @p value is an integer, or a string holding exactly one.
inline bool parseInt(nlohmann::json const& value, int& out)
{
    if (value.is_number_integer()) {
        out = value.get<int>();
        return true;
    }
    if (not value.is_string()) return false;
    auto const& text   = value.get_ref<std::string const&>();
    auto const  parsed = std::from_chars(text.data(), text.data() + text.size(), out);
    return parsed.ec == std::errc{} and parsed.ptr == text.data() + text.size();
}

/// @return Whether @p object holds a finite number under @p key.
inline bool numberField(nlohmann::json const& object, char const* key, double& out)
{
    if (not object.is_object()) return false;
    auto const found = object.find(key);
    if (found == object.end() or not found->is_number()) return false;
    out = found->get<double>();
    return std::isfinite(out);
}

/// @return Whether @p object holds a NUL-free string under @p key.
inline bool stringField(nlohmann::json const& object, char const* key, QString& out)
{
    if (not object.is_object()) return false;
    auto const found = object.find(key);
    if (found == object.end() or not found->is_string()) return false;
    out = QString::fromStdString(found->get<std::string>());
    return not out.contains(QChar(0));
}

}
