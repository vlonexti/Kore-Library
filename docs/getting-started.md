# Getting started

## Requirements

- CMake 3.21+
- MSVC (Visual Studio 2022 or newer)
- Network access on the first configure — ImGui and MinHook come from `FetchContent`

## Build

```bash
cmake -B build -A x64 && cmake --build build --config Release
```

**Match the game's architecture.** `-A x64` for a 64-bit game, `-A Win32` for a
32-bit one. A mismatch means injection fails outright rather than misbehaving
subtly, so this is an easy one to diagnose — but an easy one to waste an hour on
if you don't think of it.

Output: `build/examples/barony/Release/BaronyMenu.dll`.

### Using it in your own project

Via vcpkg (see [the port](../packaging/vcpkg/README.md)):

```cmake
find_package(KoreLibrary CONFIG REQUIRED)
target_link_libraries(MyPayload PRIVATE Kore::Library)
```

Or as a subdirectory:

```cmake
add_subdirectory(external/KoreLibrary)
target_link_libraries(MyPayload PRIVATE Kore::Library)
```

### Build options

| Option | Default | Notes |
|---|---|---|
| `KORE_BACKEND_OPENGL` | ON | Hooks `wglSwapBuffers` |
| `KORE_BACKEND_D3D11` | ON | Hooks `IDXGISwapChain::Present` |
| `KORE_BACKEND_D3D9` | ON | Hooks `IDirect3DDevice9::EndScene` |
| `KORE_GL_LEGACY` | OFF | Fixed-function GL2 ImGui backend, for pre-GL3 games |
| `KORE_BUILD_EXAMPLES` | ON | Builds the example payload |

Turning off backends you don't need shrinks the DLL and drops the corresponding
imports. The D3D DLLs are delay-loaded either way, so a build with everything on
still loads into an OpenGL-only game.

## Your first payload

```cpp
#include <Kore/Kore.hpp>
using namespace Kore;

class HelloWorld final : public Feature {
public:
    const char* Name() const override { return "Hello"; }
    const char* Category() const override { return "Demo"; }

    void OnRender() override {
        Render::Draw::Text({100.0f, 100.0f}, "Drawing inside the game",
                           Render::Color::Green());
    }

    void OnMenu() override {
        ImGui::SliderFloat("A knob", &m_value, 0.0f, 1.0f);
    }

private:
    float m_value = 0.5f;
};

KORE_ENTRY(options) {
    options.name = "MyMenu";
    options.onAttach = [] {
        FeatureManager::Get().Add<HelloWorld>();
    };
}
```

`KORE_ENTRY` writes `DllMain`, spins up a thread, and runs the library on it.
Its body configures a `StartupOptions&`.

## Injecting

Any standard injector works — nothing here needs manual mapping. Start the game,
then inject the DLL.

A console window appears with the log. Once you're past bring-up, set
`options.console = false`; the log still goes to
`%LOCALAPPDATA%\KoreLibrary\<name>.log`.

- **INSERT** — open/close the menu (`options.menuKey`)
- **END** — revert everything, unhook, free the DLL (`options.unloadKey`)

The menu opens automatically on load so you know it worked.

## StartupOptions

| Field | Default | Purpose |
|---|---|---|
| `name` | `"KoreLibrary"` | Names the log file, the config file, and the menu window |
| `console` | `true` | Allocate a console for log output |
| `backend` | `None` | Force a graphics backend instead of auto-detecting |
| `menuKey` | `VK_INSERT` | Opens the menu |
| `unloadKey` | `VK_END` | Unloads the library |
| `waitForModule` | empty | Block until this module loads before hooking |
| `waitTimeoutMs` | 30000 | How long to wait before giving up |
| `onAttach` | none | Register your features here |
| `onDetach` | none | Undo anything the framework doesn't know about |

`waitForModule` is the fix for injecting too early. If your injector fires at
process start, the game may not have created its renderer yet — waiting on
`SDL2.dll` or `d3d11.dll` avoids racing it.

## Where things live

```
include/Kore/       public headers, one directory per subsystem
src/                implementation
examples/barony/    example payload
docs/               this documentation
packaging/vcpkg/    the vcpkg port
```
