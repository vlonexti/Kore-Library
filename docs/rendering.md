# Rendering

## Backends

The overlay hooks the call the game makes to put a finished frame on screen.

| Backend | Hook point | Notes |
|---|---|---|
| D3D11 | `IDXGISwapChain::Present` | Also hooks `ResizeBuffers` for alt-tab |
| D3D9 | `IDirect3DDevice9::EndScene` | Also hooks `Reset` for device loss |
| OpenGL | `wglSwapBuffers` | Covers SDL games, which call through to it |

Detection runs in that order at `Overlay::Initialize()`. D3D goes first because
its detection (module loaded) is a strong signal, while `opengl32.dll` gets
pulled into plenty of processes that never draw with it.

The D3D backends never have to find the game's device. They create a throwaway
one, read the method addresses out of its vtable, and hook those — the game's
device shares the same vtable.

Force a backend when detection guesses wrong:

```cpp
options.backend = Render::Backend::OpenGL;
```

## Frame lifecycle

Every frame, on the game's render thread:

```
present hook
  └─ Overlay::EnterFrame()          in-flight counter, so unload can wait
     ├─ first frame: create ImGui context, bind platform + renderer
     ├─ ImGui_Impl<API>_NewFrame()
     ├─ ImGui_ImplWin32_NewFrame()
     ├─ ImGui::NewFrame()
     ├─ FeatureManager::Tick()      every enabled feature's OnTick
     ├─ draw callback               Menu::Draw → features' OnRender + OnMenu
     ├─ ImGui::Render()
     └─ ImGui_Impl<API>_RenderDrawData()
  └─ Overlay::LeaveFrame()
call original present
```

**Everything in a feature's `OnTick` / `OnRender` / `OnMenu` runs on the game's
render thread.** Anything they touch has to be safe for that. In practice: don't
block, don't take locks the game holds, and don't do file I/O per frame.

## Drawing

`Kore::Render::Draw`, targeting ImGui's foreground list so it renders above every
window including the menu.

```cpp
using namespace Kore::Render;

Draw::Line(a, b, Color::Green(), 1.5f);
Draw::Rect(min, max, Color::White());
Draw::RectFilled(min, max, Color::Black().WithAlpha(120));
Draw::CornerBox(min, max, Color::Green(), 1.5f);   // four brackets, reads cleanly
Draw::OutlinedBox(min, max, Color::Cyan());        // dark stroke both sides
Draw::Circle(center, radius, Color::Orange(), 2.0f);
Draw::Text(pos, "label", Color::White(), TextStyle::Outlined);
Draw::TextCentered(pos, "above a box", Color::White());
Draw::HealthBar(boxMin, boxMax, hp / maxHp);
Draw::ProgressBar(min, max, 0.6f, Color::Yellow());
Draw::Tracer(target, Color::Red().WithAlpha(180));
```

`Draw::Ready()` reports whether there's a live frame; every function is a no-op
otherwise, so you don't have to guard each call.

`TextStyle::Outlined` costs four extra draws per string but stays legible on any
background. `Shadowed` is one extra draw and is fine for most scenes.

Colours are RGBA bytes. `Color::Lerp(Red, Green, t)` is the health gradient.

## ESP and world-to-screen

Turning a world position into a screen position needs the game's view-projection
matrix — 16 contiguous floats somewhere in its memory. Finding it is the hard
part; using it is not.

```cpp
const auto vp = Memory::Read<Matrix4x4>(matrixAddress);
const Vec2 screen = Draw::ScreenSize();

if (auto pos = WorldToScreen(entityPos, vp, screen)) {
    Draw::CircleFilled(*pos, 4.0f, Color::Red());
}
```

`WorldToScreen` returns `nullopt` when the point is behind the camera. **You must
handle that case.** Skipping it is the single most common ESP bug: off-screen
entities project to mirrored coordinates and you get boxes stacked on top of
your own player.

### Entity boxes

```cpp
const Vec3 extents{0.5f, 0.5f, 1.8f};   // half-widths, roughly humanoid

if (auto box = Draw::ProjectBounds(entityPos, extents, vp)) {
    const auto& [min, max] = *box;
    Draw::CornerBox(min, max, Color::Green(), 1.5f);
    Draw::HealthBar(min, max, hp / maxHp);
    Draw::TextCentered({(min.x + max.x) * 0.5f, min.y - 16.0f}, name, Color::White());
}
```

`ProjectBounds` projects all eight corners and returns the screen-space bounding
rectangle, so the box fits the entity at any camera angle. `Draw::Box3D` draws a
true perspective wireframe instead, when orientation matters.

### Matrix layout

Engines disagree on storage order, so the convention is a parameter:

```cpp
WorldToScreen(pos, vp, screen, MatrixLayout::ColumnMajor);
```

Try `RowMajor` first (the default — Source, Unity, most engines that expose a
view-projection matrix directly). **If your boxes come out mirrored, rotated, or
tracking sideways, you picked the wrong one — switch.** That symptom means the
layout, essentially every time; it's not a bug in your entity loop.

## Verifying the overlay

The example payload's **Draw sandbox** feature renders every primitive at a
fixed position with an animated bar. If those shapes look right, your backend
and compositing are fine and any ESP problem is in your memory reads or your
matrix. Check it before debugging game-specific code.

## Styling

`Overlay::Get().SetupStyle()` applies the default dark theme. Call it again after
changing `ImGui::GetStyle()` yourself, or skip `Menu` entirely and hand your own
function to `Overlay::SetDrawCallback()` if you want a completely different look.
