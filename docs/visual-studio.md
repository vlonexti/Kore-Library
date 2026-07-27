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

vcpkg's MSBuild integration derives the triplet from your platform, and
**defaults to the dynamic one** — an `x64` project resolves to `x64-windows`.
This package is installed as `x64-windows-static`, so out of the box the header
is not on the include path. That is the whole reason a correct
`#include <Kore/Kore.hpp>` can still fail.

**Project → Properties**, *Configuration* = **All Configurations**, *Platform* =
the one you're targeting:

| Where | Setting | Value |
|---|---|---|
| **vcpkg** | **Use Static Libraries** | **Yes** |
| C/C++ → Code Generation | Runtime Library | `/MTd` for Debug, `/MT` for Release |
| C/C++ → Language | C++ Language Standard | ISO C++20 |
| General | Configuration Type | Dynamic Library (.dll) |

"Use Static Libraries" appends `-static` to the derived triplet, which is why
you don't have to type the triplet out. Runtime Library is the one setting that
must be changed per configuration, since Debug and Release differ.

Platform Toolset only matters if you've pinned an old one — the default is
already correct.

Equivalent `.vcxproj` edit:

```xml
<PropertyGroup Label="Vcpkg">
  <VcpkgEnabled>true</VcpkgEnabled>
  <VcpkgUseStatic>true</VcpkgUseStatic>
</PropertyGroup>

<ItemDefinitionGroup>
  <ClCompile>
    <LanguageStandard>stdcpp20</LanguageStandard>
    <RuntimeLibrary>MultiThreadedDebug</RuntimeLibrary>
  </ClCompile>
</ItemDefinitionGroup>
```

`<VcpkgTriplet>x64-windows-static</VcpkgTriplet>` works too and is more explicit;
the checkbox is fewer clicks.

## IntelliSense vs the compiler

vcpkg contributes its headers through **`ExternalIncludePath`**, not
`AdditionalIncludeDirectories`. IntelliSense caches that aggressively, so the
red squiggle can survive the fix.

Errors starting **`E`** (`E1696: cannot open source file`) come from
IntelliSense. Errors starting **`C`** (`C1083: Cannot open include file`) come
from the compiler. Only the second kind means the build is actually broken.

If the squiggle persists after changing the settings: **Build → Rebuild**, and
if it's still there, close and reopen the solution to force IntelliSense to
re-scan. Judge the result by the Output window, not the squiggle.

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

**`cannot open source file "Kore/Kore.hpp"`** with the include spelled
correctly — the package isn't installed for the triplet your project resolves
to. With no `<VcpkgTriplet>` set, an `x64` project defaults to `x64-windows`
(dynamic), and this package is installed as `x64-windows-static`. Set the
triplet as above. Check what you actually have with:

```bash
vcpkg list korelibrary
```

**`The following files are already installed ... and are in conflict`** — two
packages produce the same library file name. A separate `kore-library` port also
installs `lib/KoreLibrary.lib`, so the two cannot coexist in one triplet. Either
use a triplet the other package doesn't occupy (the static ones), or remove it:

```bash
vcpkg remove kore-library:x64-windows
```

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
