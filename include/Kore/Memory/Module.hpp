#pragma once

#include <Kore/Core/Types.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Kore::Memory {

/// A mapped PE image in the current process.
class Module {
public:
    /// The module the process was launched from (the .exe).
    static Module Main();

    /// Look up a loaded module by file name, e.g. "SDL2.dll". Case-insensitive.
    static std::optional<Module> Find(std::string_view name);

    /// Like Find(), but blocks until the module appears or the timeout expires.
    /// Useful when the game loads its renderer lazily.
    static std::optional<Module> WaitFor(std::string_view name, std::uint32_t timeoutMs = 30000);

    Module() = default;
    explicit Module(HMODULE handle);

    [[nodiscard]] bool Valid() const { return m_handle != nullptr; }
    explicit operator bool() const { return Valid(); }

    [[nodiscard]] HMODULE Handle() const { return m_handle; }
    [[nodiscard]] Address Base() const { return reinterpret_cast<Address>(m_handle); }
    [[nodiscard]] std::size_t Size() const { return m_size; }
    [[nodiscard]] const std::string& Name() const { return m_name; }

    /// The whole image, headers included.
    [[nodiscard]] std::span<const u8> Bytes() const;

    /// A named PE section, e.g. ".text" or ".rdata". Scanning only .text is
    /// both faster and far less prone to false positives than scanning it all.
    [[nodiscard]] std::optional<std::span<const u8>> Section(std::string_view name) const;

    /// Convenience: the executable section, falling back to the whole image.
    [[nodiscard]] std::span<const u8> Text() const;

    /// GetProcAddress, typed.
    template <typename T>
    [[nodiscard]] T Export(const char* name) const {
        return reinterpret_cast<T>(reinterpret_cast<void*>(::GetProcAddress(m_handle, name)));
    }

    [[nodiscard]] void* ExportRaw(const char* name) const;

private:
    HMODULE     m_handle = nullptr;
    std::size_t m_size   = 0;
    std::string m_name;
};

} // namespace Kore::Memory
