#include <Kore/Menu/Feature.hpp>
#include <Kore/Core/Config.hpp>
#include <Kore/Core/Logger.hpp>

#include <Windows.h>

#include <algorithm>
#include <format>

namespace Kore {
namespace {

std::string EnabledKey(const Feature& feature) {
    return std::format("{}.Enabled", feature.Name());
}

std::string HotkeyKey(const Feature& feature) {
    return std::format("{}.Hotkey", feature.Name());
}

} // namespace

void Feature::SetEnabled(bool enabled) {
    if (enabled == m_enabled)
        return;

    m_enabled = enabled;

    // A feature that throws out of OnEnable would leave the flag lying, so
    // report rather than let it escape into the game's render thread.
    try {
        if (enabled)
            OnEnable();
        else
            OnDisable();
    } catch (const std::exception& e) {
        KORE_ERROR("Feature '{}' threw during {}: {}", Name(), enabled ? "OnEnable" : "OnDisable", e.what());
    }

    Config::Get().Set(EnabledKey(*this), enabled);
}

FeatureManager& FeatureManager::Get() {
    static FeatureManager instance;
    return instance;
}

void FeatureManager::Register(std::unique_ptr<Feature> feature) {
    if (!feature)
        return;
    KORE_TRACE("Registered feature '{}' [{}]", feature->Name(), feature->Category());
    m_features.push_back(std::move(feature));
}

void FeatureManager::AttachAll() {
    auto& config = Config::Get();

    for (auto& feature : m_features) {
        feature->OnAttach();

        if (const int hotkey = config.GetInt(HotkeyKey(*feature), feature->Hotkey()); hotkey != 0)
            feature->SetHotkey(hotkey);

        // Restore persisted toggles last, so OnEnable sees a fully attached
        // feature. The fallback is whatever state OnAttach left the feature in,
        // which is how a feature opts into being on by default; a saved setting
        // always wins over that.
        if (feature->Toggleable()) {
            const bool defaultEnabled = feature->Enabled();
            feature->SetEnabled(config.GetBool(EnabledKey(*feature), defaultEnabled));
        }
    }

    KORE_INFO("{} feature(s) attached", m_features.size());
}

void FeatureManager::DisableAll() {
    // Reverse order, mirroring construction: later features may depend on
    // patches earlier ones installed.
    for (auto it = m_features.rbegin(); it != m_features.rend(); ++it) {
        if ((*it)->Enabled()) {
            try {
                (*it)->OnDisable();
            } catch (const std::exception& e) {
                KORE_ERROR("Feature '{}' threw during unload: {}", (*it)->Name(), e.what());
            }
        }
    }
}

namespace {

/// Non-toggleable features (a watermark, a HUD element) have no checkbox, so
/// there is nothing to switch them on — they simply always run.
bool ShouldRun(const Feature& feature) {
    return !feature.Toggleable() || feature.Enabled();
}

} // namespace

void FeatureManager::Tick() {
    for (auto& feature : m_features) {
        if (ShouldRun(*feature))
            feature->OnTick();
    }
}

void FeatureManager::Render() {
    for (auto& feature : m_features) {
        if (ShouldRun(*feature))
            feature->OnRender();
    }
}

void FeatureManager::HandleHotkeys() {
    for (auto& feature : m_features) {
        const int key = feature->Hotkey();
        if (key == 0 || !feature->Toggleable())
            continue;

        // GetAsyncKeyState's low bit means "pressed since the last query",
        // which gives us edge detection without tracking state ourselves.
        if (::GetAsyncKeyState(key) & 1)
            feature->SetEnabled(!feature->Enabled());
    }
}

std::vector<std::string> FeatureManager::Categories() const {
    std::vector<std::string> categories;
    for (const auto& feature : m_features) {
        const std::string category = feature->Category();
        if (std::find(categories.begin(), categories.end(), category) == categories.end())
            categories.push_back(category);
    }
    return categories;
}

Feature* FeatureManager::Find(std::string_view name) const {
    for (const auto& feature : m_features) {
        if (name == feature->Name())
            return feature.get();
    }
    return nullptr;
}

} // namespace Kore
