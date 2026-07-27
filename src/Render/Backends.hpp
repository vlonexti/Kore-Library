#pragma once

#include <Kore/Render/IRenderBackend.hpp>

#include <memory>

namespace Kore::Render {

/// Factories for the compiled-in backends. Overlay::Initialize() tries each in
/// turn; a backend whose CMake option is off simply isn't declared here.
#if defined(KORE_BACKEND_OPENGL)
std::unique_ptr<IRenderBackend> MakeOpenGLBackend();
#endif

#if defined(KORE_BACKEND_D3D11)
std::unique_ptr<IRenderBackend> MakeD3D11Backend();
#endif

#if defined(KORE_BACKEND_D3D9)
std::unique_ptr<IRenderBackend> MakeD3D9Backend();
#endif

namespace Detail {

/// Create the ImGui context and bind the Win32 platform layer. A backend calls
/// this on its first hooked frame, then installs its own renderer binding
/// (ImGui_ImplOpenGL3_Init, ImGui_ImplDX11_Init, ...) on top.
bool InitImGuiCore(HWND window);

} // namespace Detail

} // namespace Kore::Render
