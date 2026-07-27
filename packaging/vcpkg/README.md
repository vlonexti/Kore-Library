# vcpkg port

## Install

The port lives here rather than in vcpkg's own `ports/` tree, so use it as an
overlay:

```bash
vcpkg install korelibrary --overlay-ports=C:/Users/Steven/Documents/Code/KoreLibrary/packaging/vcpkg/ports --triplet x64-windows-static
```

Or copy it into your vcpkg checkout to skip the flag on every command:

```bash
cp -r packaging/vcpkg/ports/korelibrary C:/dev/vcpkg/ports/
```

**Use a static triplet** — `x64-windows-static` or `x86-windows-static`. An
injected DLL should not depend on loose runtime DLLs sitting next to the game
executable, and the library is static-only (`vcpkg_check_linkage`).

Pick the triplet architecture to match the game: `x64-windows-static` for a
64-bit game, `x86-windows-static` for a 32-bit one.

## Features

| Feature | Default | Backend |
|---|---|---|
| `opengl` | yes | Hooks `wglSwapBuffers` |
| `d3d11` | yes | Hooks `IDXGISwapChain::Present` |
| `d3d9` | no | Hooks `IDirect3DDevice9::EndScene` |

```bash
vcpkg install "korelibrary[opengl,d3d9]" --triplet x64-windows-static
```

Each feature pulls the matching ImGui binding, so you only build the backends
you asked for.

## Using it

```cmake
find_package(KoreLibrary CONFIG REQUIRED)

add_library(MyPayload SHARED Main.cpp)
target_link_libraries(MyPayload PRIVATE Kore::Library)
set_target_properties(MyPayload PROPERTIES PREFIX "")
```

Configure with the vcpkg toolchain:

```bash
cmake -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
```

`Kore::Library` and `Kore::KoreLibrary` both work; the docs use the former.

## Updating the port after a release

`portfile.cmake` pins a tag and its archive hash. After tagging a new version:

1. Bump `version` in `vcpkg.json` to match the tag.
2. Get the new hash — the quickest way is to let vcpkg tell you:

```bash
vcpkg install korelibrary --overlay-ports=./ports --triplet x64-windows-static
```

It fails on the hash mismatch and prints the actual SHA512. Paste that into
`portfile.cmake`.

## Local development

To build against a working tree instead of a tagged release, skip the port and
use `add_subdirectory` — the top-level `CMakeLists.txt` falls back to
`FetchContent` for ImGui and MinHook when `KORE_USE_EXTERNAL_DEPS` is off, so no
dependency setup is needed.
