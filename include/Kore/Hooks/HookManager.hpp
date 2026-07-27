#pragma once

#include <Kore/Core/Types.hpp>

namespace Kore::Hooks {

/// Initialise the trampoline engine. Must be called once before any Detour is
/// created; Overlay::Initialize() does this for you.
bool Init();

/// Disable and free every trampoline. Call this on unload, and only after the
/// render hook has been retired — see Overlay::Shutdown().
void Shutdown();

/// Apply every pending hook in one pass. Cheaper than enabling individually
/// because the engine only suspends the process's threads once.
bool ApplyQueued();

} // namespace Kore::Hooks
