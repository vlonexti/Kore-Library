#pragma once

#include <Kore/Core/Types.hpp>

#include <functional>

namespace Kore::Hooks {

/// Window-procedure hook, installed with SetWindowLongPtr. This is how the
/// overlay receives keyboard and mouse input, and how it swallows input while
/// the menu is open so the game doesn't also act on it.
class WndProcHook {
public:
    /// Return true to consume the message (the game will never see it).
    using Filter = std::function<bool(HWND, UINT, WPARAM, LPARAM)>;

    static WndProcHook& Get();

    bool Install(HWND window, Filter filter);
    void Remove();

    [[nodiscard]] HWND Window() const { return m_window; }
    [[nodiscard]] bool Installed() const { return m_original != nullptr; }

    /// Forward to the game's original handler.
    LRESULT CallOriginal(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) const;

private:
    static LRESULT CALLBACK Dispatch(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND    m_window   = nullptr;
    WNDPROC m_original = nullptr;
    Filter  m_filter;
};

} // namespace Kore::Hooks
