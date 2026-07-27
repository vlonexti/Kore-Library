# Hooking

Namespace `Kore::Hooks`. `Overlay::Initialize()` calls `Hooks::Init()` for you;
call it yourself only if you hook something before standing up the overlay.

## Detours

An inline trampoline hook, backed by MinHook. The original function's first
instructions are relocated so you can still call through.

```cpp
Kore::Hooks::Detour g_shoot;

void __fastcall Shoot_hk(void* weapon, void* player) {
    KORE_INFO("shot fired");
    g_shoot.Call<decltype(&Shoot_hk)>(weapon, player);
}

// in OnAttach
const Address target = Memory::Pattern("40 53 48 83 EC ??").Scan(Memory::Module::Main());
g_shoot.Create(target, &Shoot_hk);
g_shoot.Enable();
```

**Match the calling convention exactly.** A `__fastcall` target hooked with a
`__cdecl` detour corrupts the stack, and the crash lands somewhere unrelated
minutes later. On x64 there is only one convention so this is a non-issue;
on x86 it bites constantly.

`Call<Fn>` returns a default-constructed value if the hook has already been torn
down, which keeps unload races from crashing.

Hooking an exported API by name:

```cpp
detour.CreateApi(L"user32.dll", "SetCursorPos", &SetCursorPos_hk);
```

## Vtable hooks

For virtual calls, prefer this over a detour. It swaps a pointer in the table
rather than patching code, so it's invisible to code-integrity checks and
reversible with a single write.

```cpp
Kore::Hooks::VmtHook hook;
hook.Init(someObject);

auto original = hook.Hook<PresentFn>(8, &Present_hk);
// ...
hook.Unhook(8);      // or UnhookAll(), which the destructor also does
```

The vtable is shared by every instance of the class, so hooking one object hooks
them all. That's usually what you want.

`EstimateSize()` walks the table until it finds a non-code pointer. Use it when
you don't know the index yet — log the entries and match them against the
interface header.

## Input

```cpp
Hooks::WndProcHook::Get().Install(hwnd, [](HWND h, UINT m, WPARAM w, LPARAM l) {
    return m == WM_KEYDOWN && w == VK_F1;   // true = swallow it
});
```

The overlay installs this itself and uses it to route messages to ImGui, toggle
the menu, and swallow input the menu is using so the game doesn't also act on
it. You rarely need to install your own.

Returning `true` consumes the message — the game never sees it. This is what
stops you from shooting through the menu.

## Teardown

Order matters, and `Overlay::Shutdown()` gets it right:

1. Remove the input hook, so no new message reaches a dying ImGui context.
2. Remove the render hook.
3. Wait for the render thread to leave the hook (bounded, 2 seconds).
4. Shut down ImGui.
5. `Hooks::Shutdown()` — disables all hooks in one pass, which suspends threads
   once and guarantees nobody is parked inside a trampoline when it's freed.

Getting this wrong produces the classic unload crash: the DLL's pages are freed
while a game thread is still executing inside them.

If you install your own hooks, remove them in `Feature::OnDisable` — the
`FeatureManager` disables every feature before the overlay shuts down.
