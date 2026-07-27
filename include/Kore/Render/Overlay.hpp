#pragma once

#include <Kore/Core/Types.hpp>
#include <Kore/Render/IRenderBackend.hpp>

#include <atomic>
#include <functional>
#include <memory>

namespace Kore::Render {

/// The overlay: owns the render-API hook, the ImGui context, the input hook,
/// and the per-frame callback that features draw into.
///
/// Lifetime is deliberately explicit. Initialize() runs on your loader thread;
/// every draw callback runs on the game's render thread, so anything they touch
/// needs to be safe for that.
class Overlay {
public:
    static Overlay& Get();

    /// Detect the graphics API and hook it. `preferred` forces a backend when
    /// detection would guess wrong (a game that links both D3D and GL, say).
    bool Initialize(Backend preferred = Backend::None);

    /// Retire the render hook, wait for any in-flight frame to leave it, then
    /// release ImGui. Safe to call from the unload thread.
    void Shutdown();

    [[nodiscard]] bool Running() const { return m_running.load(std::memory_order_acquire); }
    [[nodiscard]] Backend Kind() const;
    [[nodiscard]] HWND Window() const;

    /// Called every frame between ImGui::NewFrame() and ImGui::Render().
    void SetDrawCallback(std::function<void()> callback);

    /// Toggled by the menu key; features can read it to pause game input.
    [[nodiscard]] bool MenuOpen() const { return m_menuOpen.load(std::memory_order_relaxed); }
    void SetMenuOpen(bool open);
    void ToggleMenu() { SetMenuOpen(!MenuOpen()); }

    /// Virtual-key that toggles the menu. Defaults to VK_INSERT.
    void SetMenuKey(int vk) { m_menuKey = vk; }
    [[nodiscard]] int MenuKey() const { return m_menuKey; }

    // ---- called by backends; not part of the public surface ----------------

    /// Per-frame entry from a backend's present hook. Handles first-frame
    /// setup, the ImGui frame, and the draw callback.
    void RenderFrame(HWND window, const std::function<void()>& platformNewFrame,
                     const std::function<void()>& platformRenderDrawData);

    /// Backends call this once they know the window, so input can be hooked.
    void AttachInput(HWND window);

    [[nodiscard]] bool ImGuiReady() const { return m_imguiReady; }
    void MarkImGuiReady(bool ready) { m_imguiReady = ready; }

    /// Incremented while a thread is inside the present hook. Shutdown() spins
    /// on this reaching zero before it unhooks, so we never free a trampoline
    /// out from under the render thread.
    void EnterFrame() { m_inFrame.fetch_add(1, std::memory_order_acq_rel); }
    void LeaveFrame() { m_inFrame.fetch_sub(1, std::memory_order_acq_rel); }

    /// Apply the default dark theme. Called during first-frame setup; call it
    /// again yourself if you want to restyle after the fact.
    void SetupStyle();

private:
    Overlay() = default;

    bool HandleMessage(HWND, UINT, WPARAM, LPARAM);

    std::unique_ptr<IRenderBackend> m_backend;
    std::function<void()>           m_draw;
    std::atomic<bool>               m_running{false};
    std::atomic<bool>               m_menuOpen{false};
    std::atomic<int>                m_inFrame{0};
    bool                            m_imguiReady = false;
    bool                            m_inputHooked = false;
    bool                            m_menuKeyDown = false;
    int                             m_menuKey = VK_INSERT;
};

} // namespace Kore::Render
