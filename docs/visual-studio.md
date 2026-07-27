# Using KoreLibrary from Visual Studio

Once the package is installed globally and vcpkg's MSBuild integration is on,
any C++ project can `#include <Kore/Kore.hpp>` with no include paths, no library
paths, and no per-project install — the same way `imgui` and `minhook` work.

## One-time setup

```bash
vcpkg integrate install
```

```bash
vcpkg install korelibrary:x64-windows-static korelibrary:x86-windows-static
```

Both are already done on this machine. `integrate install` is user-wide, so it
applies to every solution you open.

## Per-project settings

Three settings, because the default vcpkg triplet is dynamic and a payload
wants static linkage.

**Project → Properties**, with *Configuration* set to **All Configurations** and
*Platform* to the one you're targeting:

| Where | Setting | Value |
|---|---|---|
| General | Platform Toolset | must match the toolset vcpkg built with (`v145` here) |
| C/C++ → Language | C++ Language Standard | ISO C++20 |
| C/C++ → Code Generation | Runtime Library | Multi-threaded (`/MT`), or `/MTd` for Debug |
| vcpkg | Triplet | `x64-windows-static` (or `x86-windows-static`) |
| General | Configuration Type | Dynamic Library (.dll) |

Or edit the `.vcxproj` directly:

```xml
<PropertyGroup Label="Vcpkg">
  <VcpkgEnabled>true</VcpkgEnabled>
  <VcpkgTriplet>x64-windows-static</VcpkgTriplet>
</PropertyGroup>

<PropertyGroup Label="Configuration">
  <ConfigurationType>DynamicLibrary</ConfigurationType>
  <PlatformToolset>v145</PlatformToolset>
</PropertyGroup>

<ItemDefinitionGroup>
  <ClCompile>
    <LanguageStandard>stdcpp20</LanguageStandard>
    <RuntimeLibrary>MultiThreaded</RuntimeLibrary>
  </ClCompile>
</ItemDefinitionGroup>
```

That's it. `#include <Kore/Kore.hpp>` and build.

## Minimal payload

```cpp
#include <Kore/Kore.hpp>

using namespace Kore;

class Example final : public Feature {
public:
    const char* Name() const override { return "Example"; }
    void OnMenu() override { ImGui::Text("hello"); }
};

KORE_ENTRY(options) {
    options.name = "MyMenu";
    options.onAttach = [] { FeatureManager::Get().Add<Example>(); };
}
```

## Errors you will hit

**`cannot open source file "Kore.hpp"`** — the include is
`<Kore/Kore.hpp>`, not `<Kore.hpp>`. The `Kore/` prefix is part of the path.

**`unresolved external symbol __std_find_first_not_of_trivial_pos_1`** (or a
similar `__std_*` symbol) — toolset mismatch. The package was built by vcpkg
with a newer MSVC than your project's Platform Toolset, and the newer STL
headers reference CRT helpers the older toolset's libraries don't contain. Set
Platform Toolset to the version vcpkg used.

**`LNK2038: mismatch detected for 'RuntimeLibrary'`** — your project is on
`/MD` while the static triplet is `/MT`. Change Runtime Library to
Multi-threaded, per configuration. Debug needs `/MTd`.

**`unresolved external symbol D3D11CreateDeviceAndSwapChain`** — fixed in
v0.1.2; the system libraries are declared with `#pragma comment(lib, ...)` now.
Update the package if you're on something older.

**Architecture mismatch at injection time** — the triplet and the project
platform must both match the game. `x64-windows-static` with Platform `x64` for
a 64-bit game; `x86-windows-static` with `Win32` for a 32-bit one.

## Debug builds

Use `/MTd` and the Debug configuration as normal. The overlay works identically;
the log just carries more detail if you call `Logger::SetMinLevel`.

Note that attaching the Visual Studio debugger to a game you've injected into
works fine for your own code — set breakpoints in your feature and they'll hit
on the render thread.
