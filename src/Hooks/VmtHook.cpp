#include <Kore/Hooks/VmtHook.hpp>
#include <Kore/Memory/Pattern.hpp>
#include <Kore/Memory/Protect.hpp>
#include <Kore/Core/Logger.hpp>

#include <Windows.h>

namespace Kore::Hooks {
namespace {

bool LooksLikeCode(void* address) {
    if (!address)
        return false;
    MEMORY_BASIC_INFORMATION info{};
    if (::VirtualQuery(address, &info, sizeof(info)) == 0)
        return false;
    if (info.State != MEM_COMMIT)
        return false;
    constexpr DWORD kExecutable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                  PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (info.Protect & kExecutable) != 0;
}

} // namespace

VmtHook::~VmtHook() {
    UnhookAll();
}

bool VmtHook::Init(void* instance) {
    if (!instance || !Memory::IsReadable(reinterpret_cast<Address>(instance), sizeof(void*))) {
        KORE_ERROR("VmtHook::Init got an unreadable instance pointer");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(instance);
    if (!vtable || !Memory::IsReadable(reinterpret_cast<Address>(vtable), sizeof(void*))) {
        KORE_ERROR("VmtHook::Init: instance has no readable vtable");
        return false;
    }

    m_vtable = vtable;
    return true;
}

void* VmtHook::Hook(std::size_t index, void* replacement) {
    if (!m_vtable || !replacement)
        return nullptr;

    void** slot = &m_vtable[index];
    if (!Memory::IsReadable(reinterpret_cast<Address>(slot), sizeof(void*))) {
        KORE_ERROR("VmtHook: index {} is out of the readable vtable range", index);
        return nullptr;
    }

    // Only record the original the first time, so re-hooking an index doesn't
    // lose the game's real function.
    if (!m_originals.contains(index))
        m_originals[index] = *slot;

    // Vtables usually live in .rdata, so the write needs the page opened up.
    Memory::ScopedProtect guard(reinterpret_cast<Address>(slot), sizeof(void*), PAGE_READWRITE);
    if (!guard.Ok())
        return nullptr;

    *slot = replacement;
    KORE_TRACE("VmtHook: index {} -> {}", index, replacement);
    return m_originals[index];
}

void* VmtHook::Original(std::size_t index) const {
    const auto it = m_originals.find(index);
    return it == m_originals.end() ? nullptr : it->second;
}

bool VmtHook::Unhook(std::size_t index) {
    const auto it = m_originals.find(index);
    if (it == m_originals.end() || !m_vtable)
        return false;

    void** slot = &m_vtable[index];
    Memory::ScopedProtect guard(reinterpret_cast<Address>(slot), sizeof(void*), PAGE_READWRITE);
    if (!guard.Ok())
        return false;

    *slot = it->second;
    m_originals.erase(it);
    return true;
}

void VmtHook::UnhookAll() {
    if (!m_vtable)
        return;
    while (!m_originals.empty())
        Unhook(m_originals.begin()->first);
}

std::size_t VmtHook::EstimateSize() const {
    if (!m_vtable)
        return 0;

    std::size_t count = 0;
    constexpr std::size_t kSanityLimit = 512;
    while (count < kSanityLimit) {
        void** slot = &m_vtable[count];
        if (!Memory::IsReadable(reinterpret_cast<Address>(slot), sizeof(void*)))
            break;
        if (!LooksLikeCode(*slot))
            break;
        ++count;
    }
    return count;
}

} // namespace Kore::Hooks
