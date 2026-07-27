#include <Kore/Render/IRenderBackend.hpp>
#include <Kore/Render/Overlay.hpp>
#include <Kore/Hooks/Detour.hpp>
#include <Kore/Core/Logger.hpp>

#include "Render/Backends.hpp"
#include "Render/ImGuiBackends.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

// Declared here rather than only in CMake so the dependency travels with the
// object file. vcpkg's MSBuild integration links the packages' .lib files but
// has no way to know about the Windows system libs a package needs.
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace Kore::Render {
namespace {

// IDXGISwapChain vtable layout: IUnknown (0-2), IDXGIObject (3-6),
// IDXGIDeviceSubObject (7), then the swap chain's own methods.
constexpr std::size_t kPresentIndex       = 8;
constexpr std::size_t kResizeBuffersIndex = 13;

using PresentFn       = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

/// Stand up a throwaway device and swap chain purely to read the addresses out
/// of its vtable. The game's own swap chain shares that vtable, so hooking
/// these addresses hooks the real one — and we never have to find the game's
/// swap chain pointer.
bool ResolveSwapChainMethods(void** outPresent, void** outResize) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = ::DefWindowProcW;
    wc.hInstance     = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = L"KoreDummyD3D11";

    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        KORE_ERROR("Failed to register the dummy window class");
        return false;
    }

    const HWND dummy = ::CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                         0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!dummy) {
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        KORE_ERROR("Failed to create the dummy window");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount       = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow      = dummy;
    desc.SampleDesc.Count  = 1;
    desc.Windowed          = TRUE;
    desc.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL obtained{};

    IDXGISwapChain*      swapChain = nullptr;
    ID3D11Device*        device    = nullptr;
    ID3D11DeviceContext* context   = nullptr;

    const HRESULT hr = ::D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels,
        static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
        &desc, &swapChain, &device, &obtained, &context);

    bool ok = false;
    if (SUCCEEDED(hr) && swapChain) {
        void** vtable = *reinterpret_cast<void***>(swapChain);
        *outPresent = vtable[kPresentIndex];
        *outResize  = vtable[kResizeBuffersIndex];
        ok = true;
    } else {
        KORE_ERROR("D3D11CreateDeviceAndSwapChain failed: {:#x}", static_cast<unsigned>(hr));
    }

    if (swapChain) swapChain->Release();
    if (context)   context->Release();
    if (device)    device->Release();

    ::DestroyWindow(dummy);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

class D3D11Backend final : public IRenderBackend {
public:
    Backend Kind() const override { return Backend::D3D11; }
    const char* Name() const override { return "D3D11"; }

    bool Detect() const override {
        return ::GetModuleHandleW(L"d3d11.dll") != nullptr &&
               ::GetModuleHandleW(L"dxgi.dll") != nullptr;
    }

    bool Hook() override {
        void* present = nullptr;
        void* resize  = nullptr;
        if (!ResolveSwapChainMethods(&present, &resize))
            return false;

        s_self = this;

        if (!m_present.Create(present, &PresentDetour) || !m_present.Enable()) {
            KORE_ERROR("Failed to hook IDXGISwapChain::Present");
            s_self = nullptr;
            return false;
        }

        // Not fatal if this one fails — we just lose clean alt-tab/resize
        // handling rather than the whole overlay.
        if (m_resize.Create(resize, &ResizeBuffersDetour))
            m_resize.Enable();

        KORE_INFO("Hooked IDXGISwapChain::Present at {}", present);
        return true;
    }

    void Unhook() override {
        m_present.Remove();
        m_resize.Remove();

        if (m_rendererReady) {
            ImGui_ImplDX11_Shutdown();
            m_rendererReady = false;
        }

        ReleaseRenderTarget();

        if (m_context) { m_context->Release(); m_context = nullptr; }
        if (m_device)  { m_device->Release();  m_device  = nullptr; }

        s_self = nullptr;
    }

    HWND Window() const override { return m_window; }

private:
    static HRESULT STDMETHODCALLTYPE PresentDetour(IDXGISwapChain* swapChain, UINT sync, UINT flags) {
        D3D11Backend* self = s_self;
        Overlay& overlay = Overlay::Get();

        if (self && overlay.Running()) {
            overlay.EnterFrame();
            self->DrawFrame(swapChain);
            overlay.LeaveFrame();
        }

        return self ? self->m_present.Call<PresentFn>(swapChain, sync, flags)
                    : S_OK;
    }

    static HRESULT STDMETHODCALLTYPE ResizeBuffersDetour(IDXGISwapChain* swapChain, UINT count,
                                                         UINT width, UINT height,
                                                         DXGI_FORMAT format, UINT flags) {
        D3D11Backend* self = s_self;
        if (!self)
            return S_OK;

        // The back buffer can't be resized while we hold a view on it.
        self->ReleaseRenderTarget();

        const HRESULT hr = self->m_resize.Call<ResizeBuffersFn>(swapChain, count, width, height, format, flags);

        // The next Present rebuilds the view against the new back buffer.
        return hr;
    }

    void DrawFrame(IDXGISwapChain* swapChain) {
        Overlay& overlay = Overlay::Get();

        if (!m_device && !AcquireDevice(swapChain))
            return;

        if (!overlay.ImGuiReady()) {
            if (!Detail::InitImGuiCore(m_window))
                return;
            if (!ImGui_ImplDX11_Init(m_device, m_context)) {
                KORE_ERROR("ImGui_ImplDX11_Init failed");
                return;
            }
            m_rendererReady = true;
            overlay.MarkImGuiReady(true);
            KORE_INFO("D3D11 overlay ready on window {}", static_cast<void*>(m_window));
        }

        if (!m_renderTarget && !CreateRenderTarget(swapChain))
            return;

        overlay.RenderFrame(
            m_window,
            [] { ImGui_ImplDX11_NewFrame(); },
            [this] {
                m_context->OMSetRenderTargets(1, &m_renderTarget, nullptr);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            });
    }

    bool AcquireDevice(IDXGISwapChain* swapChain) {
        if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&m_device)))) {
            KORE_ERROR("IDXGISwapChain::GetDevice failed");
            return false;
        }
        m_device->GetImmediateContext(&m_context);

        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(swapChain->GetDesc(&desc)))
            m_window = desc.OutputWindow;

        return m_window != nullptr && m_context != nullptr;
    }

    bool CreateRenderTarget(IDXGISwapChain* swapChain) {
        ID3D11Texture2D* backBuffer = nullptr;
        if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer))))
            return false;

        const HRESULT hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTarget);
        backBuffer->Release();

        if (FAILED(hr)) {
            KORE_ERROR("CreateRenderTargetView failed: {:#x}", static_cast<unsigned>(hr));
            return false;
        }
        return true;
    }

    void ReleaseRenderTarget() {
        if (m_renderTarget) {
            m_renderTarget->Release();
            m_renderTarget = nullptr;
        }
    }

    Hooks::Detour m_present;
    Hooks::Detour m_resize;

    ID3D11Device*           m_device       = nullptr;
    ID3D11DeviceContext*    m_context      = nullptr;
    ID3D11RenderTargetView* m_renderTarget = nullptr;
    HWND                    m_window       = nullptr;
    bool                    m_rendererReady = false;

    static inline D3D11Backend* s_self = nullptr;
};

} // namespace

std::unique_ptr<IRenderBackend> MakeD3D11Backend() {
    return std::make_unique<D3D11Backend>();
}

} // namespace Kore::Render
