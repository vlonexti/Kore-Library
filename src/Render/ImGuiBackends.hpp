#pragma once

/// ImGui's platform/renderer bindings live under `backends/` in the upstream
/// tree, but package managers (vcpkg among them) flatten them into the include
/// root. __has_include picks whichever layout is present so the same source
/// builds against both.

#include <imgui.h>

#if __has_include(<backends/imgui_impl_win32.h>)
#  include <backends/imgui_impl_win32.h>
#else
#  include <imgui_impl_win32.h>
#endif

#if defined(KORE_BACKEND_OPENGL)
#  if defined(KORE_GL_LEGACY)
#    if __has_include(<backends/imgui_impl_opengl2.h>)
#      include <backends/imgui_impl_opengl2.h>
#    else
#      include <imgui_impl_opengl2.h>
#    endif
#  else
#    if __has_include(<backends/imgui_impl_opengl3.h>)
#      include <backends/imgui_impl_opengl3.h>
#    else
#      include <imgui_impl_opengl3.h>
#    endif
#  endif
#endif

#if defined(KORE_BACKEND_D3D11)
#  if __has_include(<backends/imgui_impl_dx11.h>)
#    include <backends/imgui_impl_dx11.h>
#  else
#    include <imgui_impl_dx11.h>
#  endif
#endif

#if defined(KORE_BACKEND_D3D9)
#  if __has_include(<backends/imgui_impl_dx9.h>)
#    include <backends/imgui_impl_dx9.h>
#  else
#    include <imgui_impl_dx9.h>
#  endif
#endif
