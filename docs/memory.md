# Memory

`#include <Kore/Kore.hpp>`, namespace `Kore::Memory`.

## Modules

```cpp
auto main = Memory::Module::Main();          // the .exe
auto sdl  = Memory::Module::Find("SDL2.dll");        // optional<Module>
auto late = Memory::Module::WaitFor("game.dll", 30000); // blocks until loaded
```

```cpp
module.Base()          // load address
module.Size()          // image size
module.Name()          // file name
module.Text()          // the .text section as a span
module.Section(".data")// any named section
module.Export<Fn>("SomeExport")
```

Scan `.text`, not the whole image. It's faster and it avoids matching your
signature against string literals in `.rdata` that happen to contain the same
bytes.

## Reading and writing

```cpp
int   hp    = Memory::Read<int>(address);        // 0 if unmapped
float speed = Memory::Read<float>(address, 1.0f); // explicit fallback
Vec3  pos   = Memory::Read<Vec3>(address);

Memory::Poke<int>(address, 100);
Memory::Write(address, bytes);                    // span of u8
Memory::Nop(address, 5);
```

`Read<T>` validates the address first and returns the fallback rather than
faulting. That check costs a `VirtualQuery`; in a loop over thousands of
entities per frame it's worth hoisting — check the base pointer once, then read
the fields directly.

Strings and blobs:

```cpp
std::string name = Memory::ReadString(address, 64);
std::string wide = Memory::ReadWideString(address, 64);  // UTF-16 → UTF-8
std::vector<u8>  = Memory::ReadBytes(address, 32);
Memory::WriteString(address, "new name");  // never grows the buffer
```

## Signature scanning

Hardcoded offsets break on every game patch. Signatures usually don't.

```cpp
const Memory::Pattern pattern("48 8B 05 ? ? ? ? 48 85 C0 74 ?");
const Address hit = pattern.Scan(Memory::Module::Main());
if (!hit) { KORE_WARN("signature miss"); return; }
```

`?` and `??` are both wildcards. Wildcard every operand that could shift between
builds — addresses, displacements, immediates — and keep the opcodes.

`ScanAll` returns every match. Use it while developing: **a good signature has
exactly one hit.** More than one means it's too generic and will silently latch
onto the wrong function after a patch.

### RIP-relative operands

On x64, `mov rax, [rip+disp32]` encodes a displacement, not an address. To get
what it points at:

```cpp
// 48 8B 05 xx xx xx xx  → operand at +3, instruction is 7 bytes
const Address target = Memory::ResolveRelative(hit, 3, 7);
```

## Pointer chains

```cpp
Memory::Pointer<int> health{ module.Base() + 0x1A2B3C, { 0x10, 0x8, 0x1C } };

if (auto hp = health.Get())  // nullopt if any hop is unmapped
    ImGui::Text("HP: %d", *hp);

health.Set(100);
int safe = health.GetOr(0);
```

Resolved fresh on every access, with each hop validated. Caching the final
address is how you end up reading a freed allocation after the game moves the
object — which it will, on level change, respawn, or entity despawn.

## Patching code

Every patch must be revertible. An internal DLL gets unloaded while the game
keeps running; leftover patched bytes pointing into freed code is a crash.

```cpp
class Godmode final : public Feature {
    void OnAttach() override {
        const Address site = Memory::Pattern("89 41 ?? 8B 45 ??").Scan(Memory::Module::Main());
        if (!site) return;
        m_patch = Memory::Patch(site, std::array<u8, 3>{0x90, 0x90, 0x90});
    }
    void OnEnable()  override { m_patch.Apply(); }
    void OnDisable() override { m_patch.Revert(); }

    Memory::Patch m_patch;
};
```

`Patch` snapshots the original bytes at construction, handles page protection,
and flushes the instruction cache. Do not hand-roll `VirtualProtect` +
`memcpy` — the cache flush is easy to forget and produces a bug that only
reproduces sometimes.

For a scoped unprotect:

```cpp
{
    Memory::ScopedProtect guard(address, size);
    if (guard.Ok()) { /* write */ }
}   // protection restored, icache flushed
```

## Freezing values

```cpp
Memory::Freezer::Get().Hold("ammo", address, 999);
Memory::Freezer::Get().Release("ammo");
Memory::Freezer::Get().Tick();   // call once per frame from a feature's OnTick
```

This is the blunt instrument: it rewrites the value every frame instead of
stopping the game from writing it. Use it when you can't find the writing
instruction. When you can, patch the instruction instead — one operation per
session rather than one per frame, and no flicker.

## Validity

```cpp
Memory::IsReadable(address, size);
```

Walks the region with `VirtualQuery`, rejecting uncommitted pages, `PAGE_NOACCESS`,
and guard pages. Everything above uses it internally; call it directly when you
want to check a pointer before a hot loop.
