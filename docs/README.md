# KoreLibrary documentation

An internal menu framework for Windows games, in C++20.

## Contents

| Page | What's in it |
|---|---|
| [Getting started](getting-started.md) | Build, inject, write your first feature |
| [Swed64 vs KoreLibrary](swed64-vs-kore.md) | What changes when you move from Swed64, with an API mapping table |
| [Memory](memory.md) | Signature scanning, patching, pointer chains, freezing |
| [Hooking](hooking.md) | Detours, vtable hooks, input |
| [Rendering](rendering.md) | Overlay lifecycle, backends, drawing, ESP and world-to-screen |
| [Features and menu](features.md) | The feature lifecycle, hotkeys, config |
| [Troubleshooting](troubleshooting.md) | It didn't inject / no overlay / it crashed on unload |

## Internal vs external

This matters more than any other design decision here, so it's worth being
precise about.

An **external** tool is a separate process. It opens a handle to the game and
uses `ReadProcessMemory` / `WriteProcessMemory` to peek and poke. Its overlay is
a second, transparent window floating on top. Swed64 is external.

An **internal** library is a DLL loaded *into* the game. It shares the game's
address space, so reading memory is a plain pointer dereference. That difference
compounds:

|  | External | Internal |
|---|---|---|
| Reading a value | `ReadProcessMemory` syscall | `*(int*)addr` |
| Cost per read | ~microseconds | ~nanoseconds |
| Calling game functions | not possible | just call them |
| Hooking game code | not possible | detours, vtable swaps |
| Drawing | separate overlay window, can desync | inside the game's own frame |
| Blast radius of a bug | your process crashes | the game crashes |

That last row is the tradeoff. Internal is far more capable and gives up all
the safety of process isolation. A null dereference in your feature is a game
crash, which is why `Kore::Memory` validates before it reads.

## Architecture

```
        your payload DLL
               │
        ┌──────▼───────┐
        │  KORE_ENTRY  │  DllMain → thread → Kore::Run
        └──────┬───────┘
               │
    ┌──────────▼──────────┐
    │      Overlay        │  detect API, hook presentation
    └──┬───────────────┬──┘
       │               │
  ┌────▼────┐    ┌─────▼──────┐
  │ Backend │    │ WndProcHook│  input, menu toggle
  │ GL/D3D  │    └────────────┘
  └────┬────┘
       │ every frame
  ┌────▼─────────────────┐
  │  FeatureManager      │  Tick() → Render()
  └────┬─────────────────┘
       │
  ┌────▼────┐  ┌────────┐  ┌──────┐
  │ Memory  │  │ Hooks  │  │ Draw │
  └─────────┘  └────────┘  └──────┘
```

The overlay owns the frame. Your features never touch the render hook directly —
they get called at the right point and use `Kore::Render::Draw` to put pixels on
screen.

## Namespaces

| Namespace | Contents |
|---|---|
| `Kore` | `Feature`, `FeatureManager`, `Menu`, `Config`, `Logger`, `Vec2/3/4`, `Matrix4x4`, `WorldToScreen` |
| `Kore::Memory` | `Module`, `Pattern`, `Patch`, `Pointer<T>`, `Freezer`, read/write helpers |
| `Kore::Hooks` | `Detour`, `VmtHook`, `WndProcHook` |
| `Kore::Render` | `Overlay`, `Color`, `Draw::*`, `Backend` |

`#include <Kore/Kore.hpp>` pulls in all of it, plus ImGui.

## Scope

This is reverse-engineering and game-modding tooling: trainers, debug overlays,
single-player mods. It contains no anti-cheat evasion and isn't built to. Games
differ in what their terms permit, particularly online — that's yours to check.
