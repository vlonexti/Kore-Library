#pragma once

/// KoreLibrary — an internal menu framework for Windows games.
///
/// Include this one header to get everything. ImGui is re-exported because
/// every feature that draws needs it.

#include <Kore/Core/Types.hpp>
#include <Kore/Core/Logger.hpp>
#include <Kore/Core/Config.hpp>
#include <Kore/Core/Entry.hpp>

#include <Kore/Math/Vector.hpp>
#include <Kore/Math/Matrix.hpp>

#include <Kore/Memory/Module.hpp>
#include <Kore/Memory/Pattern.hpp>
#include <Kore/Memory/Protect.hpp>
#include <Kore/Memory/Value.hpp>

#include <Kore/Hooks/HookManager.hpp>
#include <Kore/Hooks/Detour.hpp>
#include <Kore/Hooks/VmtHook.hpp>
#include <Kore/Hooks/WndProcHook.hpp>

#include <Kore/Render/IRenderBackend.hpp>
#include <Kore/Render/Overlay.hpp>
#include <Kore/Render/Draw.hpp>

#include <Kore/Menu/Feature.hpp>
#include <Kore/Menu/Menu.hpp>

#include <imgui.h>
