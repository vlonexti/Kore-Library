#pragma once

#include <Kore/Core/Types.hpp>

namespace Kore::Render {

enum class Backend { None, OpenGL, D3D9, D3D11 };

/// One graphics API's worth of overlay plumbing: find the frame-presentation
/// call, hook it, stand up ImGui's device objects, and tear it all down again.
///
/// Adding an API means implementing this interface and registering it in
/// Overlay::Initialize() — nothing else in the library needs to change.
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    [[nodiscard]] virtual Backend Kind() const = 0;
    [[nodiscard]] virtual const char* Name() const = 0;

    /// True if this API is actually in use by the process. Checked before Hook().
    [[nodiscard]] virtual bool Detect() const = 0;

    /// Install the present/swap hook. ImGui is initialised lazily on the first
    /// frame, because only then do we reliably know the window and device.
    virtual bool Hook() = 0;

    /// Remove the hook and release ImGui's device objects.
    virtual void Unhook() = 0;

    /// The game window, once known. Null until the first hooked frame.
    [[nodiscard]] virtual HWND Window() const = 0;
};

} // namespace Kore::Render
