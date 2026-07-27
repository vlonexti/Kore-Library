#include <Kore/Menu/Menu.hpp>
#include <Kore/Menu/Feature.hpp>
#include <Kore/Render/Overlay.hpp>
#include <Kore/Core/Config.hpp>
#include <Kore/Core/Entry.hpp>

#include <imgui.h>

#include <Windows.h>

#include <format>

namespace Kore {
namespace {

/// Human-readable name for a virtual key, for the hotkey column.
std::string KeyName(int vk) {
    if (vk == 0)
        return "—";

    const UINT scan = ::MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    wchar_t buffer[64]{};
    if (::GetKeyNameTextW(static_cast<LONG>(scan << 16), buffer, 64) > 0) {
        char narrow[64]{};
        ::WideCharToMultiByte(CP_UTF8, 0, buffer, -1, narrow, sizeof(narrow), nullptr, nullptr);
        return narrow;
    }
    return std::format("VK_{:#x}", vk);
}

void DrawFeatureRow(Feature& feature) {
    ImGui::PushID(&feature);

    if (feature.Toggleable()) {
        bool enabled = feature.Enabled();
        if (ImGui::Checkbox(feature.Name(), &enabled))
            feature.SetEnabled(enabled);
    } else {
        ImGui::TextUnformatted(feature.Name());
    }

    if (const char* description = feature.Description(); description && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    if (feature.Toggleable()) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);
        ImGui::TextDisabled("[%s]", KeyName(feature.Hotkey()).c_str());
    }

    // The feature's own controls, indented under its toggle.
    ImGui::Indent(16.0f);
    feature.OnMenu();
    ImGui::Unindent(16.0f);

    ImGui::PopID();
}

} // namespace

Menu& Menu::Get() {
    static Menu instance;
    return instance;
}

void Menu::Draw() {
    // Features that draw on-screen (ESP, HUD elements) render whether or not
    // the menu window itself is open.
    FeatureManager::Get().Render();

    if (Render::Overlay::Get().MenuOpen())
        DrawWindow();

    if (m_showDemo)
        ImGui::ShowDemoWindow(&m_showDemo);
}

void Menu::DrawWindow() {
    ImGui::SetNextWindowSize(ImVec2(520.0f, 460.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(80.0f, 80.0f), ImGuiCond_FirstUseEver);

    bool open = true;
    if (!ImGui::Begin(m_title.c_str(), &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (!m_subtitle.empty()) {
        ImGui::TextDisabled("%s", m_subtitle.c_str());
        ImGui::Separator();
    }

    auto& features = FeatureManager::Get();
    const auto categories = features.Categories();

    if (categories.empty()) {
        ImGui::TextDisabled("No features registered.");
        ImGui::TextDisabled("Add some with Kore::FeatureManager::Get().Add<T>().");
    } else if (ImGui::BeginTabBar("##kore_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (const auto& category : categories) {
            if (!ImGui::BeginTabItem(category.c_str()))
                continue;

            ImGui::BeginChild("##kore_list", ImVec2(0.0f, -34.0f));
            for (const auto& feature : features.All()) {
                if (category == feature->Category())
                    DrawFeatureRow(*feature);
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();

    if (ImGui::Button("Save config"))
        Config::Get().Save();

    ImGui::SameLine();
    if (ImGui::Button("Unload")) {
        Config::Get().Save();
        RequestUnload();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%.1f FPS", static_cast<double>(ImGui::GetIO().Framerate));

    ImGui::End();

    // Closing via the window's own X should also clear the overlay's flag,
    // otherwise input stays captured with nothing on screen.
    if (!open)
        Render::Overlay::Get().SetMenuOpen(false);
}

} // namespace Kore
