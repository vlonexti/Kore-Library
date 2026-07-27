#pragma once

#include <Kore/Core/Types.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Kore {

/// One toggleable thing the menu exposes.
///
/// Subclass it, override what you need, and register it with
/// FeatureManager::Add<T>(). The manager drives the lifecycle:
///
///   OnAttach   once, after the overlay is up
///   OnEnable   when the user flips the toggle on (and at load, if persisted)
///   OnTick     every frame while enabled — game state, memory writes
///   OnRender   every frame while enabled — ImGui draw calls (ESP, HUD)
///   OnMenu     every frame the menu is open — this feature's own controls
///   OnDisable  when toggled off, and unconditionally at unload
///
/// OnDisable must fully undo whatever OnEnable did. That is the single most
/// important rule here: a feature that can't cleanly revert turns unloading the
/// library into a crash.
class Feature {
public:
    virtual ~Feature() = default;

    [[nodiscard]] virtual const char* Name() const = 0;
    [[nodiscard]] virtual const char* Category() const { return "General"; }
    [[nodiscard]] virtual const char* Description() const { return nullptr; }

    /// Features that only draw (an FPS counter, a watermark) can return false
    /// so the menu doesn't render a pointless checkbox for them.
    [[nodiscard]] virtual bool Toggleable() const { return true; }

    virtual void OnAttach() {}
    virtual void OnEnable() {}
    virtual void OnDisable() {}
    virtual void OnTick() {}
    virtual void OnRender() {}
    virtual void OnMenu() {}

    [[nodiscard]] bool Enabled() const { return m_enabled; }
    void SetEnabled(bool enabled);

    /// Key that toggles this feature without opening the menu. 0 = unbound.
    [[nodiscard]] int Hotkey() const { return m_hotkey; }
    void SetHotkey(int vk) { m_hotkey = vk; }

private:
    bool m_enabled = false;
    int  m_hotkey  = 0;
};

/// Owns every registered feature and drives their per-frame callbacks.
class FeatureManager {
public:
    static FeatureManager& Get();

    template <typename T, typename... Args>
    T* Add(Args&&... args) {
        static_assert(std::is_base_of_v<Feature, T>, "T must derive from Kore::Feature");
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = owned.get();
        Register(std::move(owned));
        return raw;
    }

    void Register(std::unique_ptr<Feature> feature);

    /// Called once the overlay is live.
    void AttachAll();

    /// Disable everything, in reverse registration order. Called at unload.
    void DisableAll();

    void Tick();
    void Render();

    /// Poll hotkeys. Called from the input hook.
    void HandleHotkeys();

    [[nodiscard]] const std::vector<std::unique_ptr<Feature>>& All() const { return m_features; }

    /// Distinct categories, in first-seen order — the menu's tab list.
    [[nodiscard]] std::vector<std::string> Categories() const;

    [[nodiscard]] Feature* Find(std::string_view name) const;

private:
    std::vector<std::unique_ptr<Feature>> m_features;
};

} // namespace Kore
