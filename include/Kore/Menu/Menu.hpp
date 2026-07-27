#pragma once

#include <Kore/Core/Types.hpp>

#include <string>

namespace Kore {

/// The default menu window: a titled window with one tab per feature category,
/// each listing its features' toggles and custom controls.
///
/// This is a convenience, not a requirement. If you want a completely different
/// look, skip Menu entirely and hand your own function to
/// Overlay::SetDrawCallback().
class Menu {
public:
    static Menu& Get();

    void SetTitle(std::string title) { m_title = std::move(title); }
    [[nodiscard]] const std::string& Title() const { return m_title; }

    /// Extra text under the title — build stamp, game version, whatever.
    void SetSubtitle(std::string subtitle) { m_subtitle = std::move(subtitle); }

    /// Draw the menu plus every enabled feature's OnRender(). This is the
    /// function you normally pass to Overlay::SetDrawCallback().
    void Draw();

    /// Show ImGui's demo window alongside the menu. Useful while building out
    /// your own controls.
    void SetShowDemo(bool show) { m_showDemo = show; }

private:
    Menu() = default;

    void DrawWindow();

    std::string m_title    = "KoreLibrary";
    std::string m_subtitle;
    bool        m_showDemo = false;
};

} // namespace Kore
