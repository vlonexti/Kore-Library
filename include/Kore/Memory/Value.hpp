#pragma once

#include <Kore/Core/Types.hpp>
#include <Kore/Memory/Pattern.hpp>
#include <Kore/Memory/Protect.hpp>

#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace Kore::Memory {

// ------------------------------------------------------------------ strings

/// Read a NUL-terminated narrow string, stopping at `maxLength` bytes or the
/// first unreadable page. Returns what it managed to read.
[[nodiscard]] std::string ReadString(Address address, std::size_t maxLength = 256);

/// Read a NUL-terminated UTF-16 string and convert it to UTF-8.
[[nodiscard]] std::string ReadWideString(Address address, std::size_t maxLength = 256);

/// Write a narrow string plus its terminator. Never grows the allocation, so
/// only use this when you know the destination buffer is big enough.
bool WriteString(Address address, std::string_view value);

/// Read a fixed-size blob. Returns empty if any of it is unreadable.
[[nodiscard]] std::vector<u8> ReadBytes(Address address, std::size_t count);

// --------------------------------------------------------------- pointer path

/// A typed value reached through a chain of pointers, re-resolved on every
/// access.
///
/// Games move objects between frames, so caching a resolved address is how you
/// end up reading a freed allocation. This resolves from the base each time and
/// validates every hop, which costs a handful of VirtualQuery calls and saves
/// you the crash.
///
///     Memory::Pointer<int> health{ module.Base() + 0x1A2B3C, { 0x10, 0x8, 0x1C } };
///     if (auto hp = health.Get()) ImGui::Text("HP: %d", *hp);
///     health.Set(100);
template <typename T>
class Pointer {
public:
    static_assert(std::is_trivially_copyable_v<T>, "Pointer<T> requires a trivially copyable T");

    Pointer() = default;

    Pointer(Address base, std::initializer_list<Offset> offsets)
        : m_base(base), m_offsets(offsets) {}

    explicit Pointer(Address base) : m_base(base) {}

    /// The final address, or 0 if any link in the chain is unmapped.
    [[nodiscard]] Address Resolve() const {
        if (m_offsets.empty())
            return m_base;
        return FollowChain(m_base, m_offsets);
    }

    [[nodiscard]] std::optional<T> Get() const {
        const Address address = Resolve();
        if (!address || !IsReadable(address, sizeof(T)))
            return std::nullopt;
        return Read<T>(address);
    }

    [[nodiscard]] T GetOr(T fallback) const {
        return Get().value_or(fallback);
    }

    bool Set(const T& value) const {
        const Address address = Resolve();
        if (!address)
            return false;
        return Poke<T>(address, value);
    }

    [[nodiscard]] bool Valid() const { return Resolve() != 0; }

    void SetBase(Address base) { m_base = base; }
    [[nodiscard]] Address Base() const { return m_base; }

private:
    Address             m_base = 0;
    std::vector<Offset> m_offsets;
};

// ------------------------------------------------------------------ freezing

/// Holds a value at a fixed setting by rewriting it every tick.
///
/// This is the blunt instrument — it fights the game rather than stopping it
/// from writing. It's the right tool when you can't find the writing
/// instruction, and the wrong one when you can: patching the write is one
/// operation per session instead of one per frame, and it doesn't flicker.
class Freezer {
public:
    static Freezer& Get();

    /// Hold `address` at `value` until released. The tag identifies the entry
    /// for Release(); re-adding the same tag replaces it.
    template <typename T>
    void Hold(std::string tag, Address address, const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* raw = reinterpret_cast<const u8*>(&value);
        HoldBytes(std::move(tag), address, std::vector<u8>(raw, raw + sizeof(T)));
    }

    void HoldBytes(std::string tag, Address address, std::vector<u8> value);

    void Release(std::string_view tag);
    void ReleaseAll();

    [[nodiscard]] bool Held(std::string_view tag) const;
    [[nodiscard]] std::size_t Count() const;

    /// Rewrite every held value. Call once per frame — a feature's OnTick is
    /// the natural place, and the example payload wires this up for you.
    void Tick();

private:
    struct Entry {
        std::string     tag;
        Address         address = 0;
        std::vector<u8> value;
    };

    Freezer() = default;

    std::vector<Entry> m_entries;
};

} // namespace Kore::Memory
