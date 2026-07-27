# Features and menu

## The lifecycle

Subclass `Kore::Feature`, override what you need, register it with
`FeatureManager::Get().Add<T>()`.

| Callback | When | Thread |
|---|---|---|
| `OnAttach` | Once, after the overlay is live | loader |
| `OnEnable` | Toggled on, and at load if the setting was persisted | either |
| `OnTick` | Every frame while enabled | render |
| `OnRender` | Every frame while enabled — ImGui draw calls | render |
| `OnMenu` | Every frame the menu is open — this feature's controls | render |
| `OnDisable` | Toggled off, and unconditionally at unload | either |

Non-toggleable features (`Toggleable()` returns false) get no checkbox and run
unconditionally — the right shape for a watermark or a permanent HUD element.

## The one rule

**`OnDisable` must fully undo `OnEnable`.**

Unloading frees the DLL while the game keeps running. A patch left in place, a
hook left installed, a vtable entry still pointing at your code — any of those
turns "press END" into a crash, usually seconds later and somewhere that looks
unrelated.

`Memory::Patch` and `Hooks::Detour` are built around this. Use them rather than
raw writes and hand-rolled trampolines, and reverting is automatic.

```cpp
class Example final : public Feature {
public:
    const char* Name() const override { return "Example"; }
    const char* Category() const override { return "Player"; }
    const char* Description() const override { return "Tooltip text."; }

    void OnAttach() override {
        const Address site = Memory::Pattern("...").Scan(Memory::Module::Main());
        if (!site) { KORE_WARN("Example: signature miss"); return; }
        m_patch = Memory::Patch(site, std::array<u8, 3>{0x90, 0x90, 0x90});
    }

    void OnEnable()  override { m_patch.Apply(); }
    void OnDisable() override { m_patch.Revert(); }

    void OnMenu() override {
        if (!Enabled()) return;
        ImGui::SliderFloat("Amount", &m_amount, 0.0f, 100.0f);
    }

private:
    Memory::Patch m_patch;
    float m_amount = 50.0f;
};
```

Note `OnAttach` returning early on a signature miss, leaving `m_patch` invalid.
`Apply()` on an invalid patch is a no-op, so the feature degrades to doing
nothing instead of writing to address zero.

## Hotkeys

```cpp
feature->SetHotkey(VK_F1);
```

Toggles without opening the menu. Persisted to config as `<Name>.Hotkey`.
Edge-detected, so holding the key doesn't strobe.

## Categories

`Category()` becomes a menu tab. Features are grouped by it in first-seen order.
Common split: `Player`, `Visuals`, `World`, `Tools`, `Misc`.

## Config

Flat key/value, saved to `%LOCALAPPDATA%\KoreLibrary\<name>.cfg`.

```cpp
auto& config = Config::Get();
config.Set("Aim.Smoothing", 0.4f);
float smoothing = config.GetFloat("Aim.Smoothing", 0.5f);
config.Save();
```

Feature toggles and hotkeys persist automatically as `<Name>.Enabled` and
`<Name>.Hotkey`. Anything else is yours to save and restore — do it in
`OnAttach` and `OnMenu`.

Convention for keys is `Feature.Property`. The file is plain `key=value` lines
with `#` comments, editable by hand.

## Registration order

Features are attached in registration order and disabled in reverse, mirroring
construction. If one depends on a patch another installs, register the
dependency first.

Register everything inside `options.onAttach`. The framework installs the draw
callback only after that returns, which is what keeps the render thread from
iterating the feature list while it's still being built.

## Replacing the menu

`Menu` is a convenience, not a requirement:

```cpp
Render::Overlay::Get().SetDrawCallback([] {
    FeatureManager::Get().Render();      // features still draw
    MyOwnMenu();
});
```

Do this after `FeatureManager::Get().AttachAll()`, or from inside `onAttach`
where the framework sequences it correctly.
