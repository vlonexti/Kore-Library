#include <Kore/Hooks/HookManager.hpp>
#include <Kore/Hooks/Detour.hpp>
#include <Kore/Core/Logger.hpp>

#include <MinHook.h>

#include <atomic>

namespace Kore::Hooks {
namespace {

std::atomic<bool> g_initialised{false};

const char* StatusText(MH_STATUS status) {
    return MH_StatusToString(status);
}

} // namespace

bool Init() {
    bool expected = false;
    if (!g_initialised.compare_exchange_strong(expected, true))
        return true; // already up

    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        KORE_ERROR("MH_Initialize failed: {}", StatusText(status));
        g_initialised.store(false);
        return false;
    }
    KORE_TRACE("Hook engine initialised");
    return true;
}

void Shutdown() {
    if (!g_initialised.exchange(false))
        return;

    // Disabling everything in one pass suspends threads once and guarantees no
    // thread is parked inside a trampoline when we free it.
    if (const MH_STATUS status = MH_DisableHook(MH_ALL_HOOKS); status != MH_OK)
        KORE_WARN("MH_DisableHook(ALL) failed: {}", StatusText(status));

    if (const MH_STATUS status = MH_Uninitialize(); status != MH_OK)
        KORE_WARN("MH_Uninitialize failed: {}", StatusText(status));
    else
        KORE_TRACE("Hook engine shut down");
}

bool ApplyQueued() {
    const MH_STATUS status = MH_ApplyQueued();
    if (status != MH_OK) {
        KORE_ERROR("MH_ApplyQueued failed: {}", StatusText(status));
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ Detour

Detour::~Detour() {
    Remove();
}

Detour::Detour(Detour&& other) noexcept
    : m_target(other.m_target), m_original(other.m_original), m_enabled(other.m_enabled) {
    other.m_target   = nullptr;
    other.m_original = nullptr;
    other.m_enabled  = false;
}

Detour& Detour::operator=(Detour&& other) noexcept {
    if (this != &other) {
        Remove();
        m_target   = other.m_target;
        m_original = other.m_original;
        m_enabled  = other.m_enabled;
        other.m_target   = nullptr;
        other.m_original = nullptr;
        other.m_enabled  = false;
    }
    return *this;
}

bool Detour::Create(void* target, void* detour) {
    if (!target || !detour) {
        KORE_ERROR("Detour::Create called with a null target or detour");
        return false;
    }
    if (m_target) {
        KORE_WARN("Detour already created for {}; removing the old one first", m_target);
        Remove();
    }

    const MH_STATUS status = MH_CreateHook(target, detour, &m_original);
    if (status != MH_OK) {
        KORE_ERROR("MH_CreateHook({}) failed: {}", target, StatusText(status));
        m_original = nullptr;
        return false;
    }

    m_target = target;
    return true;
}

bool Detour::CreateApi(const wchar_t* module, const char* function, void* detour) {
    if (!detour)
        return false;

    void* original = nullptr;
    const MH_STATUS status = MH_CreateHookApiEx(module, function, detour, &original, &m_target);
    if (status != MH_OK) {
        KORE_ERROR("MH_CreateHookApiEx({}) failed: {}", function, StatusText(status));
        m_target = nullptr;
        return false;
    }

    m_original = original;
    return true;
}

bool Detour::Enable() {
    if (!m_target || m_enabled)
        return m_enabled;

    const MH_STATUS status = MH_EnableHook(m_target);
    if (status != MH_OK) {
        KORE_ERROR("MH_EnableHook({}) failed: {}", m_target, StatusText(status));
        return false;
    }
    m_enabled = true;
    return true;
}

bool Detour::Disable() {
    if (!m_target || !m_enabled)
        return true;

    const MH_STATUS status = MH_DisableHook(m_target);
    if (status != MH_OK) {
        KORE_ERROR("MH_DisableHook({}) failed: {}", m_target, StatusText(status));
        return false;
    }
    m_enabled = false;
    return true;
}

void Detour::Remove() {
    if (!m_target)
        return;

    Disable();
    MH_RemoveHook(m_target);
    m_target   = nullptr;
    m_original = nullptr;
}

} // namespace Kore::Hooks
