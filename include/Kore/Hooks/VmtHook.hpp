#pragma once

#include <Kore/Core/Types.hpp>

#include <unordered_map>

namespace Kore::Hooks {

/// Virtual-method-table hook. Swaps entries in an object's vtable rather than
/// patching code, so it is invisible to code-integrity checks and trivially
/// reversible. Preferred over a Detour whenever the target is a virtual call
/// (IDXGISwapChain::Present, engine entity methods, and so on).
///
/// Note the vtable is shared by every instance of the class — hooking one
/// object hooks them all. That is usually what you want.
class VmtHook {
public:
    VmtHook() = default;
    ~VmtHook();

    VmtHook(const VmtHook&)            = delete;
    VmtHook& operator=(const VmtHook&) = delete;

    /// Bind to an object's vtable. `instance` is a pointer to the object.
    bool Init(void* instance);

    /// Replace entry `index`, returning the original function.
    void* Hook(std::size_t index, void* replacement);

    template <typename Fn>
    Fn Hook(std::size_t index, void* replacement) {
        return reinterpret_cast<Fn>(Hook(index, replacement));
    }

    /// Original function for an index we hooked, or nullptr.
    [[nodiscard]] void* Original(std::size_t index) const;

    template <typename Fn>
    [[nodiscard]] Fn Original(std::size_t index) const {
        return reinterpret_cast<Fn>(Original(index));
    }

    /// Restore a single entry.
    bool Unhook(std::size_t index);

    /// Restore every entry we touched.
    void UnhookAll();

    [[nodiscard]] bool Valid() const { return m_vtable != nullptr; }

    /// Count the entries in the bound vtable by walking until a non-code
    /// pointer appears. Handy when you don't know the index yet — log the
    /// table and match against the interface header.
    [[nodiscard]] std::size_t EstimateSize() const;

private:
    void**                                   m_vtable = nullptr;
    std::unordered_map<std::size_t, void*>   m_originals;
};

} // namespace Kore::Hooks
