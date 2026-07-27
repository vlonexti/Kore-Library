#include <Kore/Memory/Protect.hpp>
#include <Kore/Core/Logger.hpp>

#include <Windows.h>

namespace Kore::Memory {

ScopedProtect::ScopedProtect(Address address, std::size_t size, DWORD protection)
    : m_address(address), m_size(size) {
    if (!address || size == 0)
        return;
    m_ok = ::VirtualProtect(reinterpret_cast<LPVOID>(address), size, protection, &m_old) != FALSE;
    if (!m_ok)
        KORE_WARN("VirtualProtect failed at {:#x} ({} bytes), error {}", address, size, ::GetLastError());
}

ScopedProtect::~ScopedProtect() {
    if (!m_ok)
        return;
    DWORD ignored = 0;
    ::VirtualProtect(reinterpret_cast<LPVOID>(m_address), m_size, m_old, &ignored);
    // Instruction cache can hold the pre-patch bytes; flush before the CPU
    // executes what we just wrote.
    ::FlushInstructionCache(::GetCurrentProcess(), reinterpret_cast<LPCVOID>(m_address), m_size);
}

bool Write(Address address, std::span<const u8> bytes) {
    if (!address || bytes.empty())
        return false;

    ScopedProtect guard(address, bytes.size());
    if (!guard.Ok())
        return false;

    std::memcpy(reinterpret_cast<void*>(address), bytes.data(), bytes.size());
    return true;
}

bool Nop(Address address, std::size_t size) {
    std::vector<u8> nops(size, 0x90);
    return Write(address, nops);
}

Patch::Patch(Address address, std::span<const u8> replacement)
    : m_address(address), m_replacement(replacement.begin(), replacement.end()) {
    if (!address || replacement.empty()) {
        m_address = 0;
        return;
    }
    if (!IsReadable(address, replacement.size())) {
        KORE_ERROR("Patch target {:#x} is not readable", address);
        m_address = 0;
        return;
    }
    const auto* original = reinterpret_cast<const u8*>(address);
    m_original.assign(original, original + replacement.size());
}

bool Patch::Apply() {
    if (!Valid() || m_applied)
        return false;
    m_applied = Write(m_address, m_replacement);
    return m_applied;
}

bool Patch::Revert() {
    if (!Valid() || !m_applied)
        return false;
    if (!Write(m_address, m_original))
        return false;
    m_applied = false;
    return true;
}

} // namespace Kore::Memory
