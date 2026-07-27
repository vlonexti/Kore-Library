#pragma once

#include <Kore/Core/Types.hpp>
#include <Kore/Hooks/HookManager.hpp>

#include <type_traits>
#include <utility>

namespace Kore::Hooks {

/// An inline (trampoline) hook on a function. The original is relocated so you
/// can still call through to it.
///
///     Kore::Hooks::Detour g_present;
///     HRESULT __stdcall Present_hk(IDXGISwapChain* sc, UINT s, UINT f) {
///         DrawOurStuff(sc);
///         return g_present.Call<decltype(&Present_hk)>(sc, s, f);
///     }
///     g_present.Create(target, &Present_hk);
///     g_present.Enable();
///
/// Non-copyable; moving transfers ownership of the trampoline.
class Detour {
public:
    Detour() = default;
    ~Detour();

    Detour(const Detour&)            = delete;
    Detour& operator=(const Detour&) = delete;
    Detour(Detour&& other) noexcept;
    Detour& operator=(Detour&& other) noexcept;

    /// Install (but do not enable) a hook on `target`.
    bool Create(void* target, void* detour);
    bool Create(Address target, void* detour) { return Create(reinterpret_cast<void*>(target), detour); }

    /// Hook an exported function by module + name. Convenient for API hooks
    /// like wglSwapBuffers or SDL_GL_SwapWindow.
    bool CreateApi(const wchar_t* module, const char* function, void* detour);

    bool Enable();
    bool Disable();
    void Remove();

    [[nodiscard]] bool Created() const { return m_target != nullptr; }
    [[nodiscard]] bool Enabled() const { return m_enabled; }
    [[nodiscard]] void* Target() const { return m_target; }

    /// The relocated original, typed.
    template <typename Fn>
    [[nodiscard]] Fn Original() const { return reinterpret_cast<Fn>(m_original); }

    /// Call the original directly. Returns a default-constructed value if the
    /// hook was already torn down, which keeps unload races from crashing.
    template <typename Fn, typename... Args>
    decltype(auto) Call(Args&&... args) const {
        using Ret = decltype(reinterpret_cast<Fn>(m_original)(std::forward<Args>(args)...));
        if (!m_original) {
            if constexpr (std::is_void_v<Ret>)
                return;
            else
                return Ret{};
        }
        return reinterpret_cast<Fn>(m_original)(std::forward<Args>(args)...);
    }

private:
    void* m_target   = nullptr;
    void* m_original = nullptr;
    bool  m_enabled  = false;
};

} // namespace Kore::Hooks
