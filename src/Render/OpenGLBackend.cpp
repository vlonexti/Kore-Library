#include <Kore/Render/IRenderBackend.hpp>
#include <Kore/Render/Overlay.hpp>
#include <Kore/Hooks/Detour.hpp>
#include <Kore/Core/Logger.hpp>

#include "Render/Backends.hpp"
#include "Render/ImGuiBackends.hpp"

#include <Windows.h>

// See the note in D3D11Backend.cpp — keeps MSBuild consumers linking cleanly.
#pragma comment(lib, "opengl32.lib")

namespace Kore::Render {
namespace {

/// OpenGL overlay, hooked at wglSwapBuffers.
///
/// Hooking the WGL entry point rather than something game-specific (SDL's
/// SDL_GL_SwapWindow, say) means this works on any OpenGL game on Windows,
/// including SDL ones like Barony — SDL calls through to wglSwapBuffers itself.
class OpenGLBackend final : public IRenderBackend {
public:
    Backend Kind() const override { return Backend::OpenGL; }
    const char* Name() const override { return "OpenGL"; }

    bool Detect() const override {
        const HMODULE gl = ::GetModuleHandleW(L"opengl32.dll");
        if (!gl)
            return false;

        // The render thread owns the GL context, not us, so wglGetCurrentContext
        // would report null here even in a live GL game. Presence of the module
        // plus a swap export is the best signal available from this thread; the
        // D3D11 backend is probed first, so a false positive is unlikely.
        return ::GetProcAddress(gl, "wglSwapBuffers") != nullptr;
    }

    bool Hook() override {
        s_self = this;

        // Games reach the swap through one of two exports depending on how
        // they set up the context; opengl32's wglSwapBuffers is the one SDL
        // and most engines end up in.
        if (m_hook.CreateApi(L"opengl32.dll", "wglSwapBuffers", &SwapBuffersDetour)) {
            KORE_INFO("Hooked opengl32!wglSwapBuffers");
        } else if (m_hook.CreateApi(L"gdi32.dll", "SwapBuffers", &SwapBuffersDetour)) {
            KORE_INFO("Hooked gdi32!SwapBuffers");
        } else {
            KORE_ERROR("Could not hook any swap-buffers entry point");
            return false;
        }

        return m_hook.Enable();
    }

    void Unhook() override {
        m_hook.Remove();

        if (m_rendererReady) {
#if defined(KORE_GL_LEGACY)
            ImGui_ImplOpenGL2_Shutdown();
#else
            ImGui_ImplOpenGL3_Shutdown();
#endif
            m_rendererReady = false;
        }

        s_self = nullptr;
    }

    HWND Window() const override { return m_window; }

private:
    static BOOL WINAPI SwapBuffersDetour(HDC hdc) {
        OpenGLBackend* self = s_self;
        Overlay& overlay = Overlay::Get();

        if (self && overlay.Running()) {
            overlay.EnterFrame();
            self->DrawFrame(hdc);
            overlay.LeaveFrame();
        }

        return self ? self->m_hook.Call<BOOL(WINAPI*)(HDC)>(hdc) : TRUE;
    }

    void DrawFrame(HDC hdc) {
        const HWND window = ::WindowFromDC(hdc);
        if (!window)
            return;

        // A game can swap on more than one DC (a debug view, an editor pane).
        // Bind to the first one we see and ignore the rest, or ImGui's state
        // gets torn between two windows.
        if (!m_window)
            m_window = window;
        if (window != m_window)
            return;

        Overlay& overlay = Overlay::Get();

        if (!overlay.ImGuiReady()) {
            if (!Detail::InitImGuiCore(m_window))
                return;

#if defined(KORE_GL_LEGACY)
            if (!ImGui_ImplOpenGL2_Init()) {
                KORE_ERROR("ImGui_ImplOpenGL2_Init failed");
                return;
            }
#else
            // Null version string lets the backend pick a GLSL version from
            // the live context, which is what we want across unknown games.
            if (!ImGui_ImplOpenGL3_Init(nullptr)) {
                KORE_ERROR("ImGui_ImplOpenGL3_Init failed");
                return;
            }
#endif
            m_rendererReady = true;
            overlay.MarkImGuiReady(true);
            KORE_INFO("OpenGL overlay ready on window {}", static_cast<void*>(m_window));
        }

        overlay.RenderFrame(
            m_window,
#if defined(KORE_GL_LEGACY)
            [] { ImGui_ImplOpenGL2_NewFrame(); },
            [] { ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData()); });
#else
            [] { ImGui_ImplOpenGL3_NewFrame(); },
            [] { ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); });
#endif
    }

    Hooks::Detour m_hook;
    HWND          m_window        = nullptr;
    bool          m_rendererReady = false;

    static inline OpenGLBackend* s_self = nullptr;
};

} // namespace

std::unique_ptr<IRenderBackend> MakeOpenGLBackend() {
    return std::make_unique<OpenGLBackend>();
}

} // namespace Kore::Render
