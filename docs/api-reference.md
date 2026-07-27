# API reference

Every public call in KoreLibrary, what it does, and when to reach for it.

`#include <Kore/Kore.hpp>` pulls in everything, plus ImGui.

---

## Reading the notation

C++ chains read left to right, each call operating on what the previous
returned. The example you'll see most:

```cpp
Memory::Module::Main().Base()
```

breaks down as:

| Piece | Meaning |
|---|---|
| `Memory::` | the namespace — `Kore::Memory` |
| `Module` | the class representing a loaded PE image |
| `::Main()` | a *static* function on that class — call it without an object. Returns a `Module` for the game's `.exe` |
| `.Base()` | a *member* function on the returned `Module`. Gives its load address as a `uintptr_t` |

So: "get the main module, then ask it where it's loaded." The result is the
number you add offsets to.

Written out longhand:

```cpp
Kore::Memory::Module mainModule = Kore::Memory::Module::Main();
Kore::Address        base       = mainModule.Base();
```

The chained form is the same thing without the intermediate variable. Use
longhand when you need the `Module` more than once — `Main()` does real work
each call.

`::` is scope (namespace or static member). `.` is "on this object".
`->` is "on the object this pointer points to".

---

## Core types

```cpp
Kore::Address   // uintptr_t — a process address. Not void*, so arithmetic works
Kore::Offset    // ptrdiff_t — signed, so backwards walks are legal
Kore::u8/u16/u32/u64, i8/i16/i32/i64
Kore::kIs64Bit  // compile-time bool
```

`Address` is a plain integer. `base + 0x1A2B3C` is exactly what you'd write in
a disassembler.

---

## Kore::Memory

### Module

A loaded PE image — the `.exe` or any `.dll`.

```cpp
Memory::Module::Main()                    // the game's .exe
Memory::Module::Find("client.dll")        // std::optional<Module>, case-insensitive
Memory::Module::WaitFor("game.dll", 30000) // blocks until loaded, or times out
```

`Find` returns `std::optional` because the module may not be loaded:

```cpp
if (auto client = Memory::Module::Find("client.dll")) {
    const Address base = client->Base();   // -> because client is an optional
}
```

`WaitFor` is the fix for injecting before the game has loaded a DLL. It polls
every 50ms.

On a `Module`:

| Call | Returns | Notes |
|---|---|---|
| `.Base()` | `Address` | Load address. Changes every run (ASLR) — never hardcode it |
| `.Size()` | `size_t` | Image size in bytes |
| `.Name()` | `const std::string&` | File name, e.g. `"client.dll"` |
| `.Text()` | `span<const u8>` | The `.text` section. **Scan this**, not the whole image |
| `.Section(".data")` | `optional<span<const u8>>` | Any named section |
| `.Export<Fn>("Name")` | `Fn` | Typed `GetProcAddress` |
| `.Valid()` | `bool` | Also usable as `if (module)` |

Scan `.Text()` rather than `.Bytes()`: it's faster, and it can't match a string
literal in `.rdata` that happens to contain your byte sequence.

### Reading and writing

```cpp
int   hp    = Memory::Read<int>(address);          // 0 if unmapped
float speed = Memory::Read<float>(address, 1.0f);  // explicit fallback
Vec3  pos   = Memory::Read<Vec3>(address);
auto  vp    = Memory::Read<Matrix4x4>(address);

Memory::Poke<int>(address, 100);      // typed write
Memory::Write(address, byteSpan);     // raw bytes, handles page protection
Memory::Nop(address, 5);              // fill with 0x90
```

`Read<T>` checks the address is mapped and returns the fallback if not, so a
stale pointer gives you a zero instead of a crash. That check costs a
`VirtualQuery` — in a loop over thousands of entities, validate the base pointer
once and read fields directly.

One template replaces Swed64's ~25 named read methods: `Read<short>`,
`Read<unsigned>`, `Read<bool>`, `Read<double>`, and so on.

### Strings and blobs

```cpp
std::string name = Memory::ReadString(address, 64);      // NUL-terminated ANSI
std::string wide = Memory::ReadWideString(address, 64);  // UTF-16 -> UTF-8
std::vector<u8> blob = Memory::ReadBytes(address, 32);
Memory::WriteString(address, "text");                    // never grows the buffer
```

`ReadString` checks readability per byte, so a string running up to a page
boundary still reads correctly.

### Pattern — signature scanning

Hardcoded offsets break on every game patch. Signatures usually survive.

```cpp
const Memory::Pattern pattern("48 8B 05 ? ? ? ? 48 85 C0");
const Address hit = pattern.Scan(Memory::Module::Main());
if (!hit) { KORE_WARN("signature miss"); return; }
```

| Call | Returns |
|---|---|
| `Pattern(sig)` | Constructor. `?` and `??` are both wildcards |
| `.Scan(module)` | `Address` of the first `.text` match, or `0` |
| `.Scan(span)` | Same, over an arbitrary range |
| `.ScanAll(span)` | `vector<Address>` of every match |
| `.Valid()` | `false` if the signature string was malformed |

Wildcard every operand that can shift between builds — addresses,
displacements, immediates — and keep the opcodes. **A good signature has exactly
one hit.** Check with `ScanAll` while developing; more than one and it will
silently latch onto the wrong function after a patch.

### ResolveRelative — x64 RIP-relative operands

On x64, `mov rax, [rip+disp32]` stores a *displacement*, not an address.

```cpp
// 48 8B 05 xx xx xx xx  →  operand at +3, instruction is 7 bytes long
const Address target = Memory::ResolveRelative(hit, 3, 7);
```

Arguments: the instruction address, the byte offset from there to the 4-byte
displacement, and the total instruction length. Your disassembler shows all
three.

### Pointer\<T\> — pointer chains

```cpp
Memory::Pointer<int> health{ base + 0x1A2B3C, { 0x10, 0x8, 0x1C } };

if (auto hp = health.Get())   // optional<int>, nullopt if any hop is unmapped
    ImGui::Text("HP: %d", *hp);

int  safe = health.GetOr(0);
bool ok   = health.Set(100);
Address a = health.Resolve();  // 0 if the chain broke
```

Re-resolved from the base on every access, with each hop validated. Caching a
resolved address is how you end up reading a freed allocation after the game
moves the object — and it will, on level change, respawn, or despawn.

Replaces the `ReadPointer(ReadPointer(base + a) + b) + c` ladders that external
cheats are full of.

### Patch — revertible code edits

```cpp
Memory::Patch patch(site, std::array<u8, 3>{0x90, 0x90, 0x90});
patch.Apply();    // writes the new bytes
patch.Revert();   // puts the originals back
patch.Applied();  // bool
patch.Valid();    // false if construction failed
```

Snapshots the original bytes at construction, handles page protection, flushes
the instruction cache. **Always use this rather than raw writes** — an internal
DLL gets unloaded while the game keeps running, and a patch left behind is a
crash.

For a scoped unprotect:

```cpp
{
    Memory::ScopedProtect guard(address, size);
    if (guard.Ok()) { /* write */ }
}   // protection restored, icache flushed
```

### Freezer — holding a value

```cpp
Memory::Freezer::Get().Hold("ammo", address, 999);
Memory::Freezer::Get().Release("ammo");
Memory::Freezer::Get().Tick();   // once per frame, from a feature's OnTick
```

Rewrites the value every frame. The blunt instrument — use it when you can't
find the writing instruction. When you can, patch the instruction instead: one
operation per session rather than one per frame, and no flicker.

### IsReadable

```cpp
if (Memory::IsReadable(address, sizeof(int))) { /* safe to dereference */ }
```

Walks the region with `VirtualQuery`, rejecting uncommitted pages,
`PAGE_NOACCESS`, and guard pages.

---

## Kore::Math

### Vec2 / Vec3 / Vec4

Plain contiguous floats, no padding — `sizeof(Vec3) == 12`. That means
`Memory::Read<Vec3>(addr)` reads a game's position field directly.

```cpp
Vec3 a{1, 2, 3};
a + b, a - b, a * 2.0f, a / 2.0f, -a
a.Length()          a.LengthSq()        // Sq skips the sqrt
a.Distance(b)       a.DistanceSq(b)     // use Sq to compare or sort
a.Dot(b)            a.Cross(b)
a.Normalized()      a.IsFinite()
```

`DistanceSq` matters when you run it over every entity every frame.

### Matrix4x4

16 contiguous floats. `Memory::Read<Matrix4x4>(addr)` reads a game's
view-projection matrix directly.

```cpp
m[0]            // flat index
m.At(row, col)  // row-major access
Matrix4x4::Identity()

Multiply(a, b)                             // row-major product
Perspective(fovYRadians, aspect, near, far)
LookAt(eye, target, up)
```

`Multiply`/`Perspective`/`LookAt` let you *build* a matrix rather than only read
one — used by the ESP sandbox to make a virtual camera.

### WorldToScreen

```cpp
std::optional<Vec2> WorldToScreen(world, viewProjection, screenSize, layout);
```

```cpp
if (auto pos = WorldToScreen(entityPos, vp, Draw::ScreenSize())) {
    Draw::CircleFilled(*pos, 4.0f, Color::Red());
}
```

**Returns `nullopt` when the point is behind the camera, and you must handle
that.** Treating it as a zero coordinate is the single most common ESP bug —
off-screen entities project to mirrored positions and pile up on your own
player.

`layout` is `MatrixLayout::RowMajor` (default) or `ColumnMajor`. Engines
disagree on storage order. If boxes come out mirrored, sheared, or tracking
sideways, switch it — that symptom means the layout essentially every time.

`ClipW(world, vp, layout)` gives the raw clip-space `w` if you want to do the
rejection yourself.

---

## Kore::Render

### Overlay

```cpp
auto& overlay = Render::Overlay::Get();

overlay.Initialize(Render::Backend::None);  // None = auto-detect
overlay.Shutdown();
overlay.SetDrawCallback([] { Menu::Get().Draw(); });
overlay.SetMenuKey(VK_INSERT);
overlay.SetMenuOpen(true);   .ToggleMenu();   .MenuOpen();
overlay.Kind();              // which backend won
overlay.Window();            // the game's HWND
overlay.SetupStyle();        // reapply the dark theme
```

`KORE_ENTRY` does `Initialize` and `SetDrawCallback` for you. Force a backend
with `Backend::OpenGL` / `D3D9` / `D3D11` when auto-detection guesses wrong.

### Color

```cpp
Color(235, 64, 64)          // r, g, b
Color(235, 64, 64, 180)     // with alpha
c.WithAlpha(120)
c.Packed()                  // 0xAABBGGRR, what ImGui wants
Color::Lerp(from, to, t)    // t clamped 0..1

Color::White() Black() Red() Green() Blue() Yellow() Orange() Cyan()
```

`Lerp(Color::Red(), Color::Green(), hp)` is the health gradient.

### Draw

Immediate-mode drawing onto the game's frame, on ImGui's *foreground* list — so
it renders above every window including the menu. Call from `OnRender()`.

```cpp
Draw::Ready()          // false when there's no live frame; every call below no-ops
Draw::ScreenSize()     // viewport in pixels — pass to WorldToScreen
Draw::ScreenCenter()

Draw::Line(from, to, color, thickness);
Draw::Rect(min, max, color, thickness, rounding);
Draw::RectFilled(min, max, color, rounding);
Draw::CornerBox(min, max, color, thickness, fraction);  // four brackets
Draw::OutlinedBox(min, max, color, thickness);          // dark stroke both sides
Draw::Circle(center, radius, color, thickness, segments);
Draw::CircleFilled(center, radius, color, segments);
Draw::Triangle(a, b, c, color, thickness);
Draw::TriangleFilled(a, b, c, color);

Draw::Text(pos, "text", color, TextStyle::Shadowed, size);
Draw::TextCentered(pos, "text", color, style, size);    // centred on pos
Draw::TextSize("text", size);                           // measure without drawing

Draw::HealthBar(boxMin, boxMax, fraction, width);       // vertical, left of the box
Draw::ProgressBar(min, max, fraction, fill, background);
Draw::Tracer(to, color, thickness, from);               // from defaults to bottom centre
```

`TextStyle::Outlined` costs four extra draws per string but stays legible on any
background. `Shadowed` is one extra draw and is fine for most scenes.

### ProjectBounds — the ESP workhorse

```cpp
std::optional<std::pair<Vec2, Vec2>> ProjectBounds(origin, extents, vp, layout);
```

```cpp
const Vec3 extents{0.5f, 0.5f, 1.8f};   // half-widths, roughly humanoid

if (auto box = Draw::ProjectBounds(entityPos, extents, vp)) {
    const auto& [min, max] = *box;
    Draw::CornerBox(min, max, Color::Green());
    Draw::HealthBar(min, max, hp / maxHp);
}
```

Projects all eight corners of a world-space box and returns the screen-space
bounding rectangle, so the box fits the entity at any camera angle. `nullopt` if
the box is entirely behind the camera.

`Draw::Box3D(origin, extents, vp, color, thickness, layout)` draws a true
perspective wireframe instead — 12 edges. Costs more and reads busier; use it
when orientation matters.

`Draw::MakeBoxCorners(origin, extents, out[8])` gives you the corners if you
want to do something else with them.

---

## Kore::Hooks

### Detour — inline trampoline hooks

```cpp
Kore::Hooks::Detour g_hook;

void __fastcall Target_hk(void* a, void* b) {
    g_hook.Call<decltype(&Target_hk)>(a, b);   // call the original
}

g_hook.Create(address, &Target_hk);
g_hook.CreateApi(L"user32.dll", "SetCursorPos", &hk);  // by export name
g_hook.Enable();   .Disable();   .Remove();
g_hook.Original<Fn>();   // the relocated original, typed
```

**Match the calling convention exactly.** A `__fastcall` target hooked with a
`__cdecl` detour corrupts the stack and crashes somewhere unrelated later. On
x64 there's only one convention; on x86 this bites constantly.

`Call<Fn>` returns a default-constructed value if the hook is already torn down,
which keeps unload races from crashing.

### VmtHook — vtable hooks

```cpp
Kore::Hooks::VmtHook hook;
hook.Init(someObject);
auto original = hook.Hook<PresentFn>(8, &Present_hk);
hook.Original<Fn>(8);
hook.Unhook(8);   .UnhookAll();
hook.EstimateSize();   // walk the table until a non-code pointer
```

Prefer this over a `Detour` for virtual calls — it swaps a pointer instead of
patching code, so it's invisible to integrity checks and reversible with one
write. The vtable is shared by every instance, so hooking one object hooks all.

### WndProcHook

```cpp
Hooks::WndProcHook::Get().Install(hwnd, [](HWND h, UINT m, WPARAM w, LPARAM l) {
    return m == WM_KEYDOWN && w == VK_F1;   // true = swallow it
});
```

The overlay installs this itself. Returning `true` consumes the message — that's
what stops you shooting through the menu.

---

## Kore::Feature

```cpp
class MyFeature final : public Feature {
public:
    const char* Name()        const override { return "Name"; }      // required
    const char* Category()    const override { return "Player"; }    // menu tab
    const char* Description() const override { return "Tooltip"; }
    bool        Toggleable()  const override { return true; }        // false = no checkbox

    void OnAttach()  override {}   // once, after the overlay is live
    void OnEnable()  override {}   // toggled on
    void OnDisable() override {}   // toggled off, and at unload
    void OnTick()    override {}   // every frame — game state
    void OnRender()  override {}   // every frame — drawing
    void OnMenu()    override {}   // menu open — this feature's controls

    Enabled();  SetEnabled(bool);  Hotkey();  SetHotkey(VK_F1);
};
```

**`OnDisable` must fully undo `OnEnable`.** The DLL gets freed while the game
keeps running; anything left behind is a crash.

`OnAttach` can call `SetEnabled(true)` to default the feature on — `AttachAll`
uses the post-`OnAttach` state as the config fallback, and a saved setting wins
over it.

Non-toggleable features run unconditionally.

### FeatureManager

```cpp
auto& fm = FeatureManager::Get();
fm.Add<MyFeature>();          // constructs, registers, returns MyFeature*
fm.Find("Name");              // Feature*, or nullptr
fm.All();                     // the vector
fm.Categories();              // tab list, first-seen order
fm.AttachAll();  fm.DisableAll();  fm.Tick();  fm.Render();
```

Register everything inside `options.onAttach`. The framework installs the draw
callback afterwards, which is what stops the render thread iterating a list
that's still being built.

---

## Kore::Menu, Config, Logger

```cpp
Menu::Get().SetTitle("Name");   .SetSubtitle("text");   .SetShowDemo(true);
Menu::Get().Draw();             // features' OnRender + the window
```

```cpp
auto& config = Config::Get();
config.GetBool("Key", false);   GetInt / GetFloat / GetString
config.Set("Key", value);       // overloaded for bool/int/float/string_view
config.Save();   .Load();   .Has(key);   .Remove(key);
```

Saved to `%LOCALAPPDATA%\KoreLibrary\<name>.cfg`. Feature toggles and hotkeys
persist automatically as `<Name>.Enabled` and `<Name>.Hotkey`.

```cpp
KORE_TRACE("...");  KORE_INFO("x = {}", x);  KORE_WARN(...);  KORE_ERROR(...);
Logger::SetMinLevel(LogLevel::Info);
```

`std::format` syntax — `{}` placeholders, `{:#x}` for hex. Goes to the console
and to `%LOCALAPPDATA%\KoreLibrary\<name>.log`.

---

## StartupOptions

```cpp
KORE_ENTRY(options) {
    options.name          = "MyMenu";       // log, config, and window title
    options.console       = true;           // false once past bring-up
    options.backend       = Render::Backend::None;   // None = auto-detect
    options.menuKey       = VK_INSERT;
    options.unloadKey     = VK_END;
    options.waitForModule = "SDL2.dll";     // wait before hooking
    options.waitTimeoutMs = 30000;
    options.onAttach      = [] { /* register features */ };
    options.onDetach      = [] { /* extra cleanup */ };
}
```

`KORE_ENTRY` writes `DllMain`, starts a thread, and runs the library on it.
`Kore::RequestUnload()` triggers an orderly shutdown from anywhere.
