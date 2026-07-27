#pragma once

#include <Kore/Core/Types.hpp>

#include <map>
#include <string>
#include <string_view>

namespace Kore {

/// Flat key/value settings, persisted to
/// %LOCALAPPDATA%\KoreLibrary\<name>.cfg as `key=value` lines.
///
/// Deliberately dependency-free — no JSON library — because an internal DLL
/// wants the smallest possible surface. Keys are conventionally
/// "Feature.Property", e.g. "Godmode.Enabled".
class Config {
public:
    static Config& Get();

    /// Loads immediately if the file exists.
    void Init(std::string_view name);

    bool Load();
    bool Save() const;

    [[nodiscard]] bool        GetBool(std::string_view key, bool fallback = false) const;
    [[nodiscard]] int         GetInt(std::string_view key, int fallback = 0) const;
    [[nodiscard]] float       GetFloat(std::string_view key, float fallback = 0.0f) const;
    [[nodiscard]] std::string GetString(std::string_view key, std::string_view fallback = {}) const;

    void Set(std::string_view key, bool value);
    void Set(std::string_view key, int value);
    void Set(std::string_view key, float value);
    void Set(std::string_view key, std::string_view value);

    [[nodiscard]] bool Has(std::string_view key) const;
    void Remove(std::string_view key);
    void Clear() { m_values.clear(); }

    [[nodiscard]] const std::string& Path() const { return m_path; }

private:
    Config() = default;

    std::map<std::string, std::string, std::less<>> m_values;
    std::string                                     m_path;
};

} // namespace Kore
