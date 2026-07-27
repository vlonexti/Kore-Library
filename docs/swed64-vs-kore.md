# Swed64 vs KoreLibrary

Swed64 is a C# **external** memory library: one class, ~400 lines, wrapping
`ReadProcessMemory` and `WriteProcessMemory` with typed helpers. It does that
job well and does nothing else — no hooking, no rendering, no menu, no
projection math.

KoreLibrary is a C++ **internal** framework. Everything Swed64 does is one line
here, because being inside the process makes reading memory a pointer
dereference rather than a syscall.

## What Swed64 gives you

- `GetProcess`, `GetModuleBase`
- `ReadBytes` / `WriteBytes`
- Typed reads: int, long, float, double, short, ushort, uint, ulong, bool, char, string, `Vector3`, `float[16]` matrix
- Typed writes: the same set
- Each with an `(address)` and an `(address, offset)` overload

That's the whole surface.

## API mapping

Swed64 needs ~50 named methods because C# generics can't express "reinterpret
these bytes as T". C++ templates can, so one function replaces the entire table.

| Swed64 | KoreLibrary |
|---|---|
| `swed.GetProcess("game")` | not needed — you're already inside it |
| `swed.GetModuleBase("game.exe")` | `Memory::Module::Main().Base()` |
| `swed.GetModuleBase("client.dll")` | `Memory::Module::Find("client.dll")->Base()` |
| `swed.ReadInt(addr)` | `Memory::Read<int>(addr)` |
| `swed.ReadInt(addr, off)` | `Memory::Read<int>(addr + off)` |
| `swed.ReadFloat(addr)` | `Memory::Read<float>(addr)` |
| `swed.ReadDouble(addr)` | `Memory::Read<double>(addr)` |
| `swed.ReadShort` / `ReadUShort` / `ReadUInt` / `ReadULong` / `ReadBool` / `ReadChar` | `Memory::Read<short>` / `<unsigned short>` / `<unsigned>` / `<unsigned long long>` / `<bool>` / `<char>` |
| `swed.ReadVec(addr)` | `Memory::Read<Vec3>(addr)` |
| `swed.ReadMatrix(addr)` | `Memory::Read<Matrix4x4>(addr)` |
| `swed.ReadPointer(addr)` | `Memory::Read<Address>(addr)` |
| `swed.ReadString(addr, len)` | `Memory::ReadString(addr, len)` |
| `swed.ReadBytes(addr, n)` | `Memory::ReadBytes(addr, n)` |
| `swed.WriteInt(addr, v)` | `Memory::Poke<int>(addr, v)` |
| `swed.WriteFloat` / `WriteBool` / `WriteVec` / … | `Memory::Poke<float>` / `<bool>` / `<Vec3>` / … |
| `swed.WriteBytes(addr, bytes)` | `Memory::Write(addr, bytes)` |
| `swed.WriteString(addr, s)` | `Memory::WriteString(addr, s)` |

`Memory::Read<T>` takes a fallback and returns it if the address is unmapped, so
a stale pointer gives you a zero instead of a crash. Swed64's reads return
garbage from a zeroed buffer in that case, silently.

Manual pointer chains — the `ReadPointer(ReadPointer(base + a) + b) + c` ladder
that external cheats are full of — become:

```cpp
Memory::Pointer<int> health{ base, { 0x10, 0x8, 0x1C } };
if (auto hp = health.Get()) { /* every hop was validated */ }
```

## What KoreLibrary adds

Everything below has no Swed64 equivalent, because none of it is possible from
outside the process.

**Hooking.** `Hooks::Detour` (trampolines via MinHook), `Hooks::VmtHook`
(vtable swaps), `Hooks::WndProcHook` (input). This is how you intercept the
game's own functions rather than just watching its data.

**Rendering inside the game's frame.** Four backends — OpenGL, D3D9, D3D11 —
detected at runtime. Your ESP is composited into the actual frame, so it can
never tear away from the game the way an external overlay window does.

**Signature scanning.** `Memory::Pattern("48 8B 05 ? ? ? ?")` with wildcards,
plus `ResolveRelative` for RIP-relative operands. External cheats hardcode
offsets and break on every patch; a signature usually survives.

**Revertible code patches.** `Memory::Patch` snapshots the original bytes, so
`Revert()` puts them back. Necessary because an internal DLL can be unloaded
while the game keeps running.

**Projection math.** `WorldToScreen` with both matrix conventions,
`Draw::ProjectBounds` for entity boxes, `Draw::Box3D` for wireframes. Swed64
gives you the matrix bytes; turning them into screen coordinates was left to you.

**Drawing primitives.** `Draw::CornerBox`, `HealthBar`, `Tracer`, outlined text,
progress bars — on ImGui's foreground list so ESP draws above the menu.

**Feature framework.** Registerable objects with a lifecycle, hotkeys, and
toggles persisted to disk, plus a tabbed menu generated from them.

**Value freezing.** `Memory::Freezer` for when you can't find the writing
instruction and have to fight the game instead of stopping it.

## When Swed64 is the better choice

Being honest about this: external has real advantages.

- **You can't inject.** Some games can't be injected into without effort that
  isn't worth it for a simple trainer.
- **A crash must not take the game down.** External bugs kill your tool only.
- **You want C#.** Swed64 plus WinForms is a much shorter path to a working GUI
  than C++ plus ImGui, if all you need is a few sliders.
- **Read-only tooling.** A map overlay or stats tracker that only reads doesn't
  benefit much from being internal.

If you want the capability — hooking, calling game functions, in-frame
rendering — internal is the only option, and that's what this library is.
