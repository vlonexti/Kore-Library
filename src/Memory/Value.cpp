#include <Kore/Memory/Value.hpp>
#include <Kore/Core/Logger.hpp>

#include <Windows.h>

#include <algorithm>
#include <cstring>

namespace Kore::Memory {

std::string ReadString(Address address, std::size_t maxLength) {
    if (!address)
        return {};

    std::string out;
    out.reserve(std::min<std::size_t>(maxLength, 64));

    for (std::size_t i = 0; i < maxLength; ++i) {
        const Address at = address + i;
        // Checked per byte because a string can run right up to a page edge;
        // validating the whole maxLength range up front would reject strings
        // that are perfectly readable for their actual length.
        if (!IsReadable(at, 1))
            break;

        const char c = *reinterpret_cast<const char*>(at);
        if (c == '\0')
            break;
        out.push_back(c);
    }

    return out;
}

std::string ReadWideString(Address address, std::size_t maxLength) {
    if (!address)
        return {};

    std::wstring wide;
    wide.reserve(std::min<std::size_t>(maxLength, 64));

    for (std::size_t i = 0; i < maxLength; ++i) {
        const Address at = address + i * sizeof(wchar_t);
        if (!IsReadable(at, sizeof(wchar_t)))
            break;

        const wchar_t c = *reinterpret_cast<const wchar_t*>(at);
        if (c == L'\0')
            break;
        wide.push_back(c);
    }

    if (wide.empty())
        return {};

    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                             nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                          out.data(), needed, nullptr, nullptr);
    return out;
}

bool WriteString(Address address, std::string_view value) {
    if (!address)
        return false;

    std::vector<u8> bytes(value.begin(), value.end());
    bytes.push_back('\0');
    return Write(address, bytes);
}

std::vector<u8> ReadBytes(Address address, std::size_t count) {
    if (!address || count == 0 || !IsReadable(address, count))
        return {};

    std::vector<u8> out(count);
    std::memcpy(out.data(), reinterpret_cast<const void*>(address), count);
    return out;
}

// ------------------------------------------------------------------ Freezer

Freezer& Freezer::Get() {
    static Freezer instance;
    return instance;
}

void Freezer::HoldBytes(std::string tag, Address address, std::vector<u8> value) {
    if (!address || value.empty())
        return;

    const auto it = std::find_if(m_entries.begin(), m_entries.end(),
                                 [&](const Entry& e) { return e.tag == tag; });

    if (it != m_entries.end()) {
        it->address = address;
        it->value   = std::move(value);
        return;
    }

    m_entries.push_back(Entry{ std::move(tag), address, std::move(value) });
}

void Freezer::Release(std::string_view tag) {
    std::erase_if(m_entries, [&](const Entry& e) { return e.tag == tag; });
}

void Freezer::ReleaseAll() {
    m_entries.clear();
}

bool Freezer::Held(std::string_view tag) const {
    return std::any_of(m_entries.begin(), m_entries.end(),
                       [&](const Entry& e) { return e.tag == tag; });
}

std::size_t Freezer::Count() const {
    return m_entries.size();
}

void Freezer::Tick() {
    for (const Entry& entry : m_entries) {
        // A frozen address can be freed by the game between frames (level
        // change, entity despawn), so re-check rather than fault.
        if (IsReadable(entry.address, entry.value.size()))
            Write(entry.address, entry.value);
    }
}

} // namespace Kore::Memory
