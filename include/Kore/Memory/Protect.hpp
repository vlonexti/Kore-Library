#pragma once

#include <Kore/Core/Types.hpp>
#include <Kore/Memory/Pattern.hpp>

#include <cstring>
#include <span>
#include <vector>

namespace Kore::Memory {

/// RAII VirtualProtect. Restores the original protection on scope exit, so an
/// early return or a throw can't leave a page writable behind your back.
class ScopedProtect {
public:
    ScopedProtect(Address address, std::size_t size, DWORD protection = PAGE_EXECUTE_READWRITE);
    ~ScopedProtect();

    ScopedProtect(const ScopedProtect&)            = delete;
    ScopedProtect& operator=(const ScopedProtect&) = delete;

    [[nodiscard]] bool Ok() const { return m_ok; }

private:
    Address     m_address = 0;
    std::size_t m_size    = 0;
    DWORD       m_old     = 0;
    bool        m_ok      = false;
};

/// Overwrite bytes at `address`, handling page protection.
bool Write(Address address, std::span<const u8> bytes);

/// Fill a range with 0x90. The classic "delete this instruction" primitive.
bool Nop(Address address, std::size_t size);

/// Read a value, returning `fallback` if the address is unmapped.
template <typename T>
[[nodiscard]] T Read(Address address, T fallback = {}) {
    if (!IsReadable(address, sizeof(T)))
        return fallback;
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

/// Typed write of a trivially-copyable value.
template <typename T>
bool Poke(Address address, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* raw = reinterpret_cast<const u8*>(&value);
    return Write(address, std::span<const u8>(raw, sizeof(T)));
}

/// Snapshot bytes so a feature can restore the original code when disabled.
/// Every patch you apply should have one of these paired with it.
class Patch {
public:
    Patch() = default;
    Patch(Address address, std::span<const u8> replacement);

    bool Apply();
    bool Revert();

    [[nodiscard]] bool Applied() const { return m_applied; }
    [[nodiscard]] bool Valid() const { return m_address != 0; }

private:
    Address         m_address = 0;
    std::vector<u8> m_original;
    std::vector<u8> m_replacement;
    bool            m_applied = false;
};

} // namespace Kore::Memory
