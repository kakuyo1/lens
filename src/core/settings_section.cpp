/**
 * @file settings_section.cpp
 * @brief The walk, the writes and the null flag behind SettingsSection; the typed reads are templates.
 */

#include "core/settings_section.h"

#include <utility>

namespace lens::core {

namespace detail {

bool textFlag(const nlohmann::json& held, bool fallback)
{
    if (!held.is_string())
        return fallback;
    const std::string& text = held.get_ref<const std::string&>();
    if (text == "true")
        return true;
    if (text == "false")
        return false;
    return fallback;
}

} // namespace detail

namespace {

/// @return The object @p path names inside @p node, or nullptr when a segment is missing or not an object.
nlohmann::json* walk(nlohmann::json* node, const std::vector<std::string>& path)
{
    for (const std::string& name : path) {
        if (!node->is_object())
            return nullptr;
        const auto found = node->find(name);
        if (found == node->end())
            return nullptr;
        node = &*found;
    }
    return node->is_object() ? node : nullptr;
}

} // namespace

SettingsSection::SettingsSection(nlohmann::json& document, std::vector<std::string> path, SettingsCommit commit)
    : document_(document), path_(std::move(path)), commit_(std::move(commit))
{
}

const nlohmann::json* SettingsSection::find() const
{
    return walk(&document_, path_);
}

nlohmann::json* SettingsSection::find()
{
    return walk(&document_, path_);
}

nlohmann::json& SettingsSection::ensure()
{
    nlohmann::json* node = &document_;
    for (const std::string& name : path_) {
        if (!node->is_object())
            *node = nlohmann::json::object();
        node = &(*node)[name];
    }
    if (!node->is_object())
        *node = nlohmann::json::object();
    return *node;
}

bool SettingsSection::has(std::string_view const key) const
{
    const nlohmann::json* node = find();
    return node != nullptr && node->contains(std::string{key});
}

void SettingsSection::setRaw(std::string_view const key, const nlohmann::json& value)
{
    ensure()[std::string{key}] = value;
    commit();
}

void SettingsSection::clear(std::string_view const key)
{
    ensure()[std::string{key}] = nullptr;
    commit();
}

void SettingsSection::remove(std::string_view const key)
{
    nlohmann::json* node = find();
    if (node == nullptr || node->erase(std::string{key}) == 0)
        return;
    commit();
}

SettingsSection SettingsSection::object(std::string_view const name) const
{
    std::vector<std::string> path = path_;
    path.emplace_back(name);
    return SettingsSection{document_, std::move(path), commit_};
}

void SettingsSection::commit() const
{
    if (commit_)
        commit_();
}

}
