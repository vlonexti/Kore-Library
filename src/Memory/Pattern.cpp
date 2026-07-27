#include <Kore/Memory/Pattern.hpp>
#include <Kore/Core/Logger.hpp>

#include <Windows.h>

#include <cctype>
#include <cstring>

namespace Kore::Memory {
namespace {

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

Pattern::Pattern(std::string_view signature) {
    std::size_t i = 0;
    while (i < signature.size()) {
        const char c = signature[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }

        if (c == '?') {
            m_bytes.push_back(0);
            m_mask.push_back(false);
            ++i;
            if (i < signature.size() && signature[i] == '?') // tolerate "??"
                ++i;
            continue;
        }

        const int hi = HexValue(c);
        const int lo = (i + 1 < signature.size()) ? HexValue(signature[i + 1]) : -1;
        if (hi < 0 || lo < 0) {
            KORE_ERROR("Malformed signature near offset {}: '{}'", i, signature);
            m_bytes.clear();
            m_mask.clear();
            return;
        }

        m_bytes.push_back(static_cast<u8>((hi << 4) | lo));
        m_mask.push_back(true);
        i += 2;
    }
}

Address Pattern::Scan(std::span<const u8> range) const {
    if (!Valid() || range.size() < m_bytes.size())
        return 0;

    const std::size_t patternSize = m_bytes.size();
    const std::size_t last = range.size() - patternSize;

    // Skip leading wildcards so the first-byte fast path is actually useful.
    std::size_t anchor = 0;
    while (anchor < patternSize && !m_mask[anchor])
        ++anchor;
    if (anchor == patternSize)
        return 0; // all wildcards — refuse rather than match everything
    const u8 anchorByte = m_bytes[anchor];

    for (std::size_t i = 0; i <= last; ++i) {
        if (range[i + anchor] != anchorByte)
            continue;

        bool matched = true;
        for (std::size_t j = 0; j < patternSize; ++j) {
            if (m_mask[j] && range[i + j] != m_bytes[j]) {
                matched = false;
                break;
            }
        }
        if (matched)
            return reinterpret_cast<Address>(range.data() + i);
    }
    return 0;
}

Address Pattern::Scan(const Module& module) const {
    if (!module.Valid())
        return 0;
    return Scan(module.Text());
}

std::vector<Address> Pattern::ScanAll(std::span<const u8> range) const {
    std::vector<Address> hits;
    if (!Valid() || range.size() < m_bytes.size())
        return hits;

    const std::size_t patternSize = m_bytes.size();
    const std::size_t last = range.size() - patternSize;

    for (std::size_t i = 0; i <= last; ++i) {
        bool matched = true;
        for (std::size_t j = 0; j < patternSize; ++j) {
            if (m_mask[j] && range[i + j] != m_bytes[j]) {
                matched = false;
                break;
            }
        }
        if (matched)
            hits.push_back(reinterpret_cast<Address>(range.data() + i));
    }
    return hits;
}

Address ResolveRelative(Address at, std::size_t operandOffset, std::size_t instructionLength) {
    if (!at || !IsReadable(at + operandOffset, sizeof(i32)))
        return 0;
    i32 displacement = 0;
    std::memcpy(&displacement, reinterpret_cast<const void*>(at + operandOffset), sizeof(displacement));
    return at + instructionLength + static_cast<Offset>(displacement);
}

bool IsReadable(Address address, std::size_t size) {
    if (!address)
        return false;

    MEMORY_BASIC_INFORMATION info{};
    Address cursor = address;
    const Address end = address + size;

    while (cursor < end) {
        if (::VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &info, sizeof(info)) == 0)
            return false;
        if (info.State != MEM_COMMIT)
            return false;
        if (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;

        const Address regionEnd = reinterpret_cast<Address>(info.BaseAddress) + info.RegionSize;
        if (regionEnd <= cursor) // guards against a pathological zero-size region
            return false;
        cursor = regionEnd;
    }
    return true;
}

Address FollowChain(Address base, std::span<const Offset> offsets) {
    Address cursor = base;
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        if (!IsReadable(cursor, sizeof(Address)))
            return 0;
        cursor = *reinterpret_cast<Address*>(cursor);
        if (!cursor)
            return 0;
        cursor += offsets[i];
    }
    return cursor;
}

} // namespace Kore::Memory
