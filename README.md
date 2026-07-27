# KoreLibrary

An internal menu framework for Windows games, in C++20.

"Internal" meaning the code runs *inside* the game process — you inject a DLL,
it hooks the game's frame-presentation call, and draws an ImGui menu on top.
That's the opposite of an external tool poking at the process from outside with
`ReadProcessMemory`, and it's what lets you call the game's own functions,
patch its code, and draw in its own render loop.

Built for SDL/OpenGL titles like Barony, but nothing in it is game-specific —
the backend is chosen at runtime.

## What's in it

| Area | What you get |
|---|---|
| `Kore::Memory` | Module lookup, IDA-style signature scanning, RIP-relative resolution, validated pointer chains, revertible byte patches, string/blob reads, value freezing |
| `Kore::Hooks` | MinHook-backed trampoline detours, vtable hooks, window-procedure hook |
| `Kore::Render` | Overlay with OpenGL, D3D9 and D3D11 backends detected at runtime, ImGui lifecycle, safe teardown |
| `Kore::Draw` | ESP primitives — corner boxes, health bars, tracers, outlined text, 3D wireframes |
| `Kore::Math` | `Vec2/3/4`, `Matrix4x4`, world-to-screen with both matrix layouts |
| `Kore::Feature` | Registerable feature objects with lifecycle callbacks, hotkeys, persisted toggles |
| `Kore::Menu` | Tabbed menu window driven by your registered features |
| `Kore::Config` | Flat key/value settings in `%LOCALAPPDATA%\KoreLibrary` |

## Documentation

Full docs live in [`docs/`](docs/README.md):

- [Getting started](docs/getting-started.md) — build, inject, first feature
- [API reference](docs/api-reference.md) — every call explained
- [Building an ESP](docs/esp-guide.md) — finding offsets in an unknown game and wiring them in
- [Visual Studio](docs/visual-studio.md) — global vcpkg setup
- [Swed64 vs KoreLibrary](docs/swed64-vs-kore.md) — API mapping if you're coming from Swed64
- [Memory](docs/memory.md) · [Hooking](docs/hooking.md) · [Rendering & ESP](docs/rendering.md) · [Features](docs/features.md)
- [Troubleshooting](docs/troubleshooting.md)

## Install

Via vcpkg:

```bash
vcpkg install korelibrary --triplet x64-windows-static
```

```cmake
find_package(KoreLibrary CONFIG REQUIRED)
target_link_libraries(MyPayload PRIVATE Kore::Library)
```

With `vcpkg integrate install`, MSBuild projects need no CMake at all — see
[docs/visual-studio.md](docs/visual-studio.md).

Backends are vcpkg features — `korelibrary[opengl,d3d9,d3d11]`. See
[packaging/vcpkg](packaging/vcpkg/README.md).

## Building from source

Needs CMake 3.21+, MSVC (VS 2022), and network access on the first configure —
ImGui and MinHook are pulled in via `FetchContent`.

```bash
cmake -B build -A x64 && cmake --build build --config Release
```

Match the game's architecture. Barony is 64-bit, so `-A x64`; for a 32-bit game
use `-A Win32`. An arch mismatch means the injector fails, not a subtle bug.

Output: `build/examples/barony/Release/BaronyMenu.dll`.

### Options

| Option | Default | Notes |
|---|---|---|
| `KORE_BACKEND_OPENGL` | ON | `wglSwapBuffers` hook |
| `KORE_BACKEND_D3D11` | ON | `IDXGISwapChain::Present` hook |
| `KORE_BACKEND_D3D9` | ON | `IDirect3DDevice9::EndScene` hook |
| `KORE_GL_LEGACY` | OFF | Use the fixed-function GL2 ImGui backend. Turn on for pre-GL3 games if the overlay renders as garbage. |
| `KORE_USE_EXTERNAL_DEPS` | OFF | Find ImGui/MinHook via `find_package` instead of fetching them. The vcpkg port turns this on. |
| `KORE_BUILD_EXAMPLES` | ON | Builds the example payload |

## Writing a payload

```cpp
#include <Kore/Kore.hpp>
using namespace Kore;

class Godmode final : public Feature {
public:
    const char* Name() const override { return "Godmode"; }
    const char* Category() const override { return "Player"; }

    void OnAttach() override {
        const Address site = Memory::Pattern("89 41 ?? 8B 45 ??").Scan(Memory::Module::Main());
        if (!site) { KORE_WARN("Godmode: signature miss"); return; }
        m_patch = Memory::Patch(site, std::array<u8, 3>{0x90, 0x90, 0x90});
    }

    void OnEnable()  override { m_patch.Apply(); }
    void OnDisable() override { m_patch.Revert(); }

private:
    Memory::Patch m_patch;
};

KORE_ENTRY(options) {
    options.name = "MyMenu";
    options.onAttach = [] { FeatureManager::Get().Add<Godmode>(); };
}
```

`KORE_ENTRY` writes `DllMain` for you and starts the library on its own thread.
Everything else is registering features.

### Feature lifecycle

```
OnAttach    once, after the overlay is live
OnEnable    toggled on (and at load, if the setting was persisted)
OnTick      every frame — read game state, apply writes
OnRender    every frame — ImGui draw calls for ESP/HUD
OnMenu      every frame the menu is open — this feature's controls
OnDisable   toggled off, and unconditionally at unload
```

**`OnDisable` must fully undo `OnEnable`.** This is the rule that matters most.
A feature that can't cleanly revert turns "press END to unload" into a crash,
because the game keeps running with patched code pointing at a DLL that just
got freed. `Memory::Patch` exists to make this easy — use it rather than raw
writes.

## Keys

- `INSERT` — open/close the menu
- `END` — revert everything, unhook, free the DLL

Both configurable via `StartupOptions`.

## Using it

Inject the built DLL into a running game with any standard injector
(`LoadLibrary`-based is fine — nothing here needs manual mapping). The console
window appears first; set `options.console = false` once you're past bring-up.

If the overlay doesn't appear, the log at
`%LOCALAPPDATA%\KoreLibrary\<name>.log` will say which backend was selected and
whether the hook took. Most first-run failures are an architecture mismatch or
the wrong graphics backend being detected — force it with
`options.backend = Render::Backend::OpenGL`.

## Finding things in a new game

The example payload ships three tools that work without knowing anything about
the game: **Loaded modules** (what to scan, which API is in use), **Memory
inspector** (read an address as several types, follow pointers), and
**Signature scanner** (test a signature and see how many hits it gets — a good
one gets exactly one).

Workflow that works: find the value in Cheat Engine, backtrace to the
instruction that writes it, copy the surrounding bytes into a signature,
wildcard the operands, then verify it gets a single hit with the scanner before
you write a feature around it.

Prefer signatures to hardcoded offsets. Offsets break on every game patch;
a well-chosen signature usually survives.

## Scope

This is modding and reverse-engineering tooling — the kind of thing you'd use
to build a trainer, a debug overlay, or a mod for a single-player game. It has
no anti-cheat evasion and isn't built to have any. Whether a given game's terms
permit loading it, especially in multiplayer, is on you to check.

## Third party

- [Dear ImGui](https://github.com/ocornut/imgui) — MIT
- [MinHook](https://github.com/TsudaKageyu/minhook) — BSD 2-Clause
