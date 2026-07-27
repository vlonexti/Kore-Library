#include <Kore/Hooks/WndProcHook.hpp>
#include <Kore/Core/Logger.hpp>

namespace Kore::Hooks {

WndProcHook& WndProcHook::Get() {
    static WndProcHook instance;
    return instance;
}

bool WndProcHook::Install(HWND window, Filter filter) {
    if (!::IsWindow(window)) {
        KORE_ERROR("WndProcHook: {} is not a valid window", static_cast<void*>(window));
        return false;
    }
    if (m_original) {
        if (m_window == window)
            return true;
        Remove();
    }

    m_window = window;
    m_filter = std::move(filter);

    ::SetLastError(0);
    m_original = reinterpret_cast<WNDPROC>(
        ::SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Dispatch)));

    if (!m_original && ::GetLastError() != 0) {
        KORE_ERROR("SetWindowLongPtr failed, error {}", ::GetLastError());
        m_window = nullptr;
        m_filter = nullptr;
        return false;
    }

    KORE_INFO("Input hooked on window {}", static_cast<void*>(window));
    return true;
}

void WndProcHook::Remove() {
    if (!m_original)
        return;

    if (::IsWindow(m_window)) {
        ::SetWindowLongPtrW(m_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_original));

        // A message may already be in flight inside our Dispatch. Pumping a
        // sentinel forces the window to drain before we drop the filter.
        // Timed out rather than a plain SendMessage: this runs on the unload
        // thread, and a game whose message loop has stalled must not wedge the
        // unload forever.
        DWORD_PTR ignored = 0;
        ::SendMessageTimeoutW(m_window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1000, &ignored);
    }

    m_original = nullptr;
    m_window   = nullptr;
    m_filter   = nullptr;
}

LRESULT WndProcHook::CallOriginal(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) const {
    if (m_original)
        return ::CallWindowProcW(m_original, hwnd, msg, wp, lp);
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK WndProcHook::Dispatch(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto& self = Get();

    if (self.m_filter && self.m_filter(hwnd, msg, wp, lp))
        return TRUE; // consumed — the game never sees it

    return self.CallOriginal(hwnd, msg, wp, lp);
}

} // namespace Kore::Hooks
