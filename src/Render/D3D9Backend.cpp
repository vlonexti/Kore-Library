#include <Kore/Render/IRenderBackend.hpp>
#include <Kore/Render/Overlay.hpp>
#include <Kore/Hooks/Detour.hpp>
#include <Kore/Core/Logger.hpp>

#include "Render/Backends.hpp"
#include "Render/ImGuiBackends.hpp"

#include <Windows.h>
#include <d3d9.h>

namespace Kore::Render {
namespace {

// IDirect3DDevice9 vtable indices. These are fixed by the interface layout and
// have been stable since the DX9 SDK shipped.
constexpr std::size_t kResetIndex    = 16;
constexpr std::size_t kEndSceneIndex = 42;

using EndSceneFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using ResetFn    = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

/// Create a throwaway device to read the addresses out of its vtable. The
/// game's device shares that vtable, so hooking these addresses hooks the real
/// device without ever having to find its pointer.
bool ResolveDeviceMethods(void** outEndScene, void** outReset) {
    IDirect3D9* d3d = ::Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) {
        KORE_ERROR("Direct3DCreate9 failed");
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = ::DefWindowProcW;
    wc.hInstance     = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = L"KoreDummyD3D9";

    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        d3d->Release();
        return false;
    }

    const HWND dummy = ::CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                         0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!dummy) {
        d3d->Release();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    D3DPRESENT_PARAMETERS params{};
    params.Windowed         = TRUE;
    params.SwapEffect       = D3DSWAPEFFECT_DISCARD;
    params.hDeviceWindow    = dummy;
    params.BackBufferFormat = D3DFMT_UNKNOWN;

    IDirect3DDevice9* device = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, dummy,
                                   D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT,
                                   &params, &device);

    // Some machines (headless, RDP, odd drivers) refuse a windowed HAL device.
    // A fullscreen request usually still succeeds, and we only need the vtable.
    if (FAILED(hr)) {
        params.Windowed                   = FALSE;
        params.BackBufferWidth            = 640;
        params.BackBufferHeight           = 480;
        params.BackBufferFormat           = D3DFMT_X8R8G8B8;
        params.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, dummy,
                               D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT,
                               &params, &device);
    }

    bool ok = false;
    if (SUCCEEDED(hr) && device) {
        void** vtable = *reinterpret_cast<void***>(device);
        *outEndScene = vtable[kEndSceneIndex];
        *outReset    = vtable[kResetIndex];
        ok = true;
        device->Release();
    } else {
        KORE_ERROR("IDirect3D9::CreateDevice failed: {:#x}", static_cast<unsigned>(hr));
    }

    d3d->Release();
    ::DestroyWindow(dummy);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

class D3D9Backend final : public IRenderBackend {
public:
    Backend Kind() const override { return Backend::D3D9; }
    const char* Name() const override { return "D3D9"; }

    bool Detect() const override {
        return ::GetModuleHandleW(L"d3d9.dll") != nullptr;
    }

    bool Hook() override {
        void* endScene = nullptr;
        void* reset    = nullptr;
        if (!ResolveDeviceMethods(&endScene, &reset))
            return false;

        s_self = this;

        if (!m_endScene.Create(endScene, &EndSceneDetour) || !m_endScene.Enable()) {
            KORE_ERROR("Failed to hook IDirect3DDevice9::EndScene");
            s_self = nullptr;
            return false;
        }

        // Without this the overlay's textures are lost on alt-tab or a
        // resolution change and the game renders a black screen.
        if (m_reset.Create(reset, &ResetDetour))
            m_reset.Enable();

        KORE_INFO("Hooked IDirect3DDevice9::EndScene at {}", endScene);
        return true;
    }

    void Unhook() override {
        m_endScene.Remove();
        m_reset.Remove();

        if (m_rendererReady) {
            ImGui_ImplDX9_Shutdown();
            m_rendererReady = false;
        }

        m_device = nullptr;
        s_self = nullptr;
    }

    HWND Window() const override { return m_window; }

private:
    static HRESULT STDMETHODCALLTYPE EndSceneDetour(IDirect3DDevice9* device) {
        D3D9Backend* self = s_self;
        Overlay& overlay = Overlay::Get();

        if (self && overlay.Running()) {
            overlay.EnterFrame();
            self->DrawFrame(device);
            overlay.LeaveFrame();
        }

        return self ? self->m_endScene.Call<EndSceneFn>(device) : D3D_OK;
    }

    static HRESULT STDMETHODCALLTYPE ResetDetour(IDirect3DDevice9* device,
                                                 D3DPRESENT_PARAMETERS* params) {
        D3D9Backend* self = s_self;
        if (!self)
            return D3D_OK;

        // Device objects must be released before the reset and rebuilt after,
        // or the device refuses to come back.
        if (self->m_rendererReady)
            ImGui_ImplDX9_InvalidateDeviceObjects();

        const HRESULT hr = self->m_reset.Call<ResetFn>(device, params);

        if (self->m_rendererReady && SUCCEEDED(hr))
            ImGui_ImplDX9_CreateDeviceObjects();

        return hr;
    }

    void DrawFrame(IDirect3DDevice9* device) {
        Overlay& overlay = Overlay::Get();

        if (!m_device) {
            m_device = device;

            D3DDEVICE_CREATION_PARAMETERS params{};
            if (SUCCEEDED(device->GetCreationParameters(&params)))
                m_window = params.hFocusWindow;

            if (!m_window)
                return;
        }

        if (!overlay.ImGuiReady()) {
            if (!Detail::InitImGuiCore(m_window))
                return;
            if (!ImGui_ImplDX9_Init(device)) {
                KORE_ERROR("ImGui_ImplDX9_Init failed");
                return;
            }
            m_rendererReady = true;
            overlay.MarkImGuiReady(true);
            KORE_INFO("D3D9 overlay ready on window {}", static_cast<void*>(m_window));
        }

        overlay.RenderFrame(
            m_window,
            [] { ImGui_ImplDX9_NewFrame(); },
            [] { ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData()); });
    }

    Hooks::Detour     m_endScene;
    Hooks::Detour     m_reset;
    IDirect3DDevice9* m_device        = nullptr;
    HWND              m_window        = nullptr;
    bool              m_rendererReady = false;

    static inline D3D9Backend* s_self = nullptr;
};

} // namespace

std::unique_ptr<IRenderBackend> MakeD3D9Backend() {
    return std::make_unique<D3D9Backend>();
}

} // namespace Kore::Render
