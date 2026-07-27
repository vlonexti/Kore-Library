# Troubleshooting

Start with the log: `%LOCALAPPDATA%\KoreLibrary\<name>.log`. It records which
backend was selected, whether each hook took, and every signature miss.

## The DLL won't inject

**Architecture mismatch.** The most common cause by a wide margin. A 32-bit DLL
cannot load into a 64-bit process or vice versa. Check the game in Task Manager
(a 32-bit process is tagged), then rebuild with the matching `-A x64` or
`-A Win32`.

**Missing dependency.** The build uses a static CRT so the VC++ redistributable
isn't needed, and the D3D DLLs are delay-loaded. If you've changed either, run
`dumpbin /imports` on the DLL and check every listed module exists.

## It injected but there's no overlay

Read the log first — it will say which backend was chosen.

**Wrong backend detected.** A game that links several graphics DLLs can fool
detection. Force it:

```cpp
options.backend = Render::Backend::OpenGL;
```

**Injected too early.** If the log shows the hook failing or no backend found,
the renderer probably hadn't loaded yet. Wait for it:

```cpp
options.waitForModule = "SDL2.dll";   // or d3d11.dll, or the game's own DLL
```

**Hook installed but nothing draws.** Check whether the log's "overlay ready"
line appeared. If it didn't, the present hook isn't being called — often a game
that renders through a path you didn't hook (D3D12, Vulkan, or a second
swapchain).

## The overlay renders as garbage

On OpenGL, a pre-GL3 game needs the fixed-function backend:

```bash
cmake -B build -A x64 -DKORE_GL_LEGACY=ON
```

Symptoms are a black or corrupt overlay while the game itself looks fine.

## The menu is open but I can't click anything

The input hook attaches to the window the backend reports. If the game has
several windows, or recreates its window after you inject, the hook may be on
the wrong one. The log records the window handle at "Input hooked on window".

Games using raw input in relative mouse mode (mouse-look) may not move the
cursor. The menu draws its own cursor via `io.MouseDrawCursor`, but if the game
is warping the pointer every frame you'll need to suppress that while the menu
is open.

## It crashes on unload

Almost always a feature whose `OnDisable` doesn't fully undo `OnEnable`. The
DLL's pages get freed while something still points into them.

Check every feature for:

- A `Memory::Patch` applied but not reverted
- A `Hooks::Detour` created but not removed
- A `VmtHook` entry still pointing at your function
- A thread you started that's still running

Use `Memory::Patch` and `Hooks::Detour` rather than raw writes — both revert in
their destructors.

## It crashes at random during play

**A cached address.** The game moved or freed the object. Use
`Memory::Pointer<T>`, which re-resolves and validates every hop, instead of
storing a resolved address.

**A signature that matched the wrong place.** Run it through the example
payload's Signature scanner: more than one hit means it's too generic. Widen it
until there's exactly one.

**Calling convention mismatch on a detour.** On x86, a `__fastcall` target
hooked with a `__cdecl` detour corrupts the stack and crashes somewhere
unrelated later. Check the target's convention in your disassembler.

## ESP boxes are mirrored, rotated, or in the wrong place

**Wrong matrix layout.** Switch `MatrixLayout::RowMajor` to `ColumnMajor` (or
back). This symptom means the layout essentially every time.

**Not handling points behind the camera.** `WorldToScreen` returns `nullopt` for
those; if you're treating that as a zero coordinate, off-screen entities pile up
at the top-left or mirror onto your own position.

**Wrong matrix address.** The view-projection matrix is often rebuilt each
frame — make sure you're reading it every frame, not caching it.

Before debugging any of the above, enable the example payload's **Draw sandbox**
feature. If its shapes render correctly, the overlay is fine and the problem is
in your reads or your matrix.

## Signature stopped working after a game update

Expected — that's what patches do. Re-find the code in a disassembler and
rebuild the signature, wildcarding more of the operands this time.

If a feature's `OnAttach` logs a miss, it should leave itself inert rather than
patching a bogus address. Write them that way and a stale signature costs you
one broken feature instead of a crash.

## The console window is embarrassing

```cpp
options.console = false;
```

The log still goes to `%LOCALAPPDATA%\KoreLibrary\<name>.log`.
