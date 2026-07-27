#include <Kore/Render/Overlay.hpp>
#include <Kore/Hooks/HookManager.hpp>
#include <Kore/Hooks/WndProcHook.hpp>
#include <Kore/Menu/Feature.hpp>
#include <Kore/Core/Logger.hpp>

#include "Render/Backends.hpp"
#include "Render/ImGuiBackends.hpp"

#include <chrono>
#include <thread>
#include <vector>

// Declared in imgui_impl_win32.cpp but intentionally left out of the header.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace Kore::Render {

Overlay& Overlay::Get() {
    static Overlay instance;
    return instance;
}

bool Overlay::Initialize(Backend preferred) {
    if (m_running.load(std::memory_order_acquire)) {
        KORE_WARN("Overlay is already running");
        return true;
    }

    if (!Hooks::Init())
        return false;

    // Probed in order. The D3D backends detect on a loaded module, which is a
    // strong signal; OpenGL goes last because opengl32.dll gets pulled into
    // plenty of processes that never draw with it.
    std::vector<std::unique_ptr<IRenderBackend>> candidates;
#if defined(KORE_BACKEND_D3D11)
    candidates.push_back(MakeD3D11Backend());
#endif
#if defined(KORE_BACKEND_D3D9)
    candidates.push_back(MakeD3D9Backend());
#endif
#if defined(KORE_BACKEND_OPENGL)
    candidates.push_back(MakeOpenGLBackend());
#endif

    if (candidates.empty()) {
        KORE_ERROR("No render backends were compiled in");
        return false;
    }

    for (auto& candidate : candidates) {
        const bool wanted = (preferred == Backend::None) || (candidate->Kind() == preferred);
        if (!wanted)
            continue;

        if (!candidate->Detect()) {
            KORE_TRACE("Backend {} not present in this process", candidate->Name());
            continue;
        }

        KORE_INFO("Using the {} backend", candidate->Name());
        if (!candidate->Hook()) {
            KORE_ERROR("Failed to hook the {} backend", candidate->Name());
            continue;
        }

        m_backend = std::move(candidate);
        m_running.store(true, std::memory_order_release);
        return true;
    }

    KORE_ERROR("No usable render backend found (preferred = {})", static_cast<int>(preferred));
    return false;
}

void Overlay::Shutdown() {
    if (!m_running.exchange(false))
        return;

    KORE_INFO("Overlay shutting down");

    // Order matters. Drop the input hook first so no new message can reach a
    // half-destroyed ImGui context.
    Hooks::WndProcHook::Get().Remove();

    if (m_backend)
        m_backend->Unhook();

    // The render thread may still be inside the present hook. Wait it out
    // before releasing ImGui — a bounded wait, because a game that has stopped
    // presenting entirely must not deadlock the unload.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (m_inFrame.load(std::memory_order_acquire) > 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (m_inFrame.load(std::memory_order_acquire) > 0)
        KORE_WARN("Render thread still inside the present hook; unloading anyway");

    if (m_imguiReady) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_imguiReady = false;
    }

    m_backend.reset();
    m_draw = nullptr;
    m_inputHooked = false;
}

Backend Overlay::Kind() const {
    return m_backend ? m_backend->Kind() : Backend::None;
}

HWND Overlay::Window() const {
    return m_backend ? m_backend->Window() : nullptr;
}

void Overlay::SetDrawCallback(std::function<void()> callback) {
    m_draw = std::move(callback);
}

void Overlay::SetMenuOpen(bool open) {
    m_menuOpen.store(open, std::memory_order_relaxed);

    if (m_imguiReady) {
        // Most games hide the OS cursor, so ImGui draws its own while the menu
        // is up. This is also what makes the menu usable in mouse-look games.
        ImGui::GetIO().MouseDrawCursor = open;
    }
}

void Overlay::AttachInput(HWND window) {
    if (m_inputHooked || !window)
        return;

    const bool ok = Hooks::WndProcHook::Get().Install(
        window, [this](HWND h, UINT m, WPARAM w, LPARAM l) { return HandleMessage(h, m, w, l); });

    m_inputHooked = ok;
}

bool Overlay::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (!m_running.load(std::memory_order_acquire) || !m_imguiReady)
        return false;

    // Edge-triggered so holding the key doesn't strobe the menu.
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        if (static_cast<int>(wparam) == m_menuKey) {
            if (!m_menuKeyDown) {
                m_menuKeyDown = true;
                ToggleMenu();
            }
            return true;
        }
        FeatureManager::Get().HandleHotkeys();
    } else if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        if (static_cast<int>(wparam) == m_menuKey)
            m_menuKeyDown = false;
    }

    ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);

    if (!MenuOpen())
        return false;

    // With the menu open, swallow anything ImGui is using so the game doesn't
    // also act on it — no shooting through the menu, no WASD leaking into a
    // text field.
    const ImGuiIO& io = ImGui::GetIO();
    const bool mouseMessage = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_MOUSEHOVER;
    const bool keyMessage   = (msg >= WM_KEYFIRST && msg <= WM_KEYLAST);

    if (io.WantCaptureMouse && mouseMessage)
        return true;
    if (io.WantCaptureKeyboard && keyMessage)
        return true;
    if (msg == WM_SETCURSOR)
        return true;

    return false;
}

void Overlay::RenderFrame(HWND window,
                          const std::function<void()>& platformNewFrame,
                          const std::function<void()>& platformRenderDrawData) {
    if (!m_running.load(std::memory_order_acquire) || !m_imguiReady)
        return;

    AttachInput(window);

    platformNewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    FeatureManager::Get().Tick();

    if (m_draw)
        m_draw();

    ImGui::Render();
    platformRenderDrawData();
}

void Overlay::SetupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.WindowPadding     = ImVec2(12.0f, 12.0f);
    style.FramePadding      = ImVec2(8.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);

    ImVec4* colours = style.Colors;
    colours[ImGuiCol_WindowBg]        = ImVec4(0.07f, 0.07f, 0.09f, 0.96f);
    colours[ImGuiCol_ChildBg]         = ImVec4(0.10f, 0.10f, 0.12f, 0.60f);
    colours[ImGuiCol_PopupBg]         = ImVec4(0.08f, 0.08f, 0.10f, 0.98f);
    colours[ImGuiCol_Border]          = ImVec4(0.24f, 0.24f, 0.30f, 0.60f);
    colours[ImGuiCol_FrameBg]         = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
    colours[ImGuiCol_FrameBgHovered]  = ImVec4(0.22f, 0.22f, 0.28f, 1.00f);
    colours[ImGuiCol_FrameBgActive]   = ImVec4(0.26f, 0.26f, 0.34f, 1.00f);
    colours[ImGuiCol_TitleBg]         = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
    colours[ImGuiCol_TitleBgActive]   = ImVec4(0.14f, 0.14f, 0.19f, 1.00f);
    colours[ImGuiCol_CheckMark]       = ImVec4(0.40f, 0.72f, 1.00f, 1.00f);
    colours[ImGuiCol_SliderGrab]      = ImVec4(0.35f, 0.62f, 0.92f, 1.00f);
    colours[ImGuiCol_SliderGrabActive]= ImVec4(0.45f, 0.72f, 1.00f, 1.00f);
    colours[ImGuiCol_Button]          = ImVec4(0.18f, 0.20f, 0.26f, 1.00f);
    colours[ImGuiCol_ButtonHovered]   = ImVec4(0.26f, 0.30f, 0.40f, 1.00f);
    colours[ImGuiCol_ButtonActive]    = ImVec4(0.32f, 0.38f, 0.52f, 1.00f);
    colours[ImGuiCol_Header]          = ImVec4(0.20f, 0.22f, 0.30f, 1.00f);
    colours[ImGuiCol_HeaderHovered]   = ImVec4(0.26f, 0.30f, 0.40f, 1.00f);
    colours[ImGuiCol_HeaderActive]    = ImVec4(0.32f, 0.38f, 0.52f, 1.00f);
    colours[ImGuiCol_Tab]             = ImVec4(0.14f, 0.15f, 0.20f, 1.00f);
    colours[ImGuiCol_TabHovered]      = ImVec4(0.26f, 0.30f, 0.40f, 1.00f);
    colours[ImGuiCol_TabSelected]     = ImVec4(0.22f, 0.26f, 0.36f, 1.00f);
    colours[ImGuiCol_Separator]       = ImVec4(0.24f, 0.24f, 0.30f, 0.60f);
}

} // namespace Kore::Render

namespace Kore::Render::Detail {

/// Shared first-frame ImGui setup. Backends call this once they have a window,
/// then install their own renderer binding on top.
bool InitImGuiCore(HWND window) {
    IMGUI_CHECKVERSION();
    if (ImGui::GetCurrentContext() == nullptr)
        ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // don't scatter imgui.ini into the game folder
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    if (!ImGui_ImplWin32_Init(window)) {
        KORE_ERROR("ImGui_ImplWin32_Init failed");
        return false;
    }

    ImGui::StyleColorsDark();
    Overlay::Get().SetupStyle();
    return true;
}

} // namespace Kore::Render::Detail
