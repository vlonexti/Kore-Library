#include <Kore/Core/Config.hpp>
#include <Kore/Core/Logger.hpp>

#include "Core/Paths.hpp"

#include <charconv>
#include <fstream>
#include <sstream>

namespace Kore {
namespace {

std::string_view Trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

} // namespace

Config& Config::Get() {
    static Config instance;
    return instance;
}

void Config::Init(std::string_view name) {
    m_path = (KoreDataDirectory() / (std::string(name) + ".cfg")).string();
    Load();
}

bool Config::Load() {
    if (m_path.empty())
        return false;

    std::ifstream file(m_path);
    if (!file.is_open()) {
        KORE_TRACE("No config at {} yet — starting from defaults", m_path);
        return false;
    }

    m_values.clear();
    std::string line;
    while (std::getline(file, line)) {
        const std::string_view view = Trim(line);
        if (view.empty() || view.front() == '#' || view.front() == ';')
            continue;

        const auto equals = view.find('=');
        if (equals == std::string_view::npos)
            continue;

        const auto key = Trim(view.substr(0, equals));
        const auto value = Trim(view.substr(equals + 1));
        if (!key.empty())
            m_values.emplace(std::string(key), std::string(value));
    }

    KORE_INFO("Loaded {} setting(s) from {}", m_values.size(), m_path);
    return true;
}

bool Config::Save() const {
    if (m_path.empty())
        return false;

    std::ofstream file(m_path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        KORE_ERROR("Could not write config to {}", m_path);
        return false;
    }

    file << "# KoreLibrary configuration — regenerated on save\n";
    for (const auto& [key, value] : m_values)
        file << key << '=' << value << '\n';

    return true;
}

bool Config::Has(std::string_view key) const {
    return m_values.find(key) != m_values.end();
}

void Config::Remove(std::string_view key) {
    if (const auto it = m_values.find(key); it != m_values.end())
        m_values.erase(it);
}

std::string Config::GetString(std::string_view key, std::string_view fallback) const {
    const auto it = m_values.find(key);
    return it == m_values.end() ? std::string(fallback) : it->second;
}

bool Config::GetBool(std::string_view key, bool fallback) const {
    const auto it = m_values.find(key);
    if (it == m_values.end())
        return fallback;
    const std::string& value = it->second;
    return value == "1" || value == "true" || value == "True";
}

int Config::GetInt(std::string_view key, int fallback) const {
    const auto it = m_values.find(key);
    if (it == m_values.end())
        return fallback;

    int parsed = 0;
    const auto& text = it->second;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    return result.ec == std::errc{} ? parsed : fallback;
}

float Config::GetFloat(std::string_view key, float fallback) const {
    const auto it = m_values.find(key);
    if (it == m_values.end())
        return fallback;

    // from_chars for floats is patchy across toolchains; istringstream is
    // slower but this only runs on load and on menu interaction.
    std::istringstream stream(it->second);
    float parsed = 0.0f;
    stream >> parsed;
    return stream.fail() ? fallback : parsed;
}

void Config::Set(std::string_view key, bool value) {
    m_values[std::string(key)] = value ? "1" : "0";
}

void Config::Set(std::string_view key, int value) {
    m_values[std::string(key)] = std::to_string(value);
}

void Config::Set(std::string_view key, float value) {
    m_values[std::string(key)] = std::format("{}", value);
}

void Config::Set(std::string_view key, std::string_view value) {
    m_values[std::string(key)] = std::string(value);
}

} // namespace Kore
