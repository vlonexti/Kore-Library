# Building an ESP for a game

End to end: what data you need, how to find it in a game you know nothing about,
how to turn what you found into code that survives updates, and how to wire it
into a feature.

This is written game-agnostically. The workflow is the same everywhere — only
the addresses change.

Before starting, run the [ESP sandbox](../../../Downloads/ESP/README.md). It's a
complete working ESP over a synthetic world, so you can see what a correct
result looks like before you go looking for one.

---

## 1. What an ESP actually needs

Only four things. Everything else is decoration.

| Data | Type | Used for |
|---|---|---|
| **View-projection matrix** | 16 floats | Turning world positions into screen positions |
| **Entity list** | array or linked list of pointers | Finding things to draw |
| **Position** | 3 floats, per entity | Where each box goes |
| **Local player** | pointer | Skipping yourself, measuring distance |

Then optional per-entity extras: health, team, name, a "is alive" flag.

**Find them in that order.** The matrix is the hardest and the most valuable —
without it nothing else can be drawn, and once you have it you can verify
everything downstream visually.

---

## 2. Finding the view-projection matrix

The matrix is 16 contiguous floats that change every frame as you move.

### Cheat Engine approach

1. Attach Cheat Engine to the game.
2. Scan type **Float**, **Unknown initial value**.
3. Stand still → **Unchanged value**. Repeat two or three times.
4. Turn the camera → **Changed value**.
5. Stand still → **Unchanged value** again.
6. Repeat until you're down to a few hundred results.

You're looking for a cluster of 16 floats at consecutive 4-byte addresses. Add
one to the address list and browse memory there — a view-projection matrix looks
distinctive: values roughly in the -1..1 range for the rotation part, larger
numbers in the translation row, and `m[15]` often exactly `1.0`.

### Confirming it

Read it and project a point you can verify:

```cpp
const auto vp = Memory::Read<Matrix4x4>(matrixAddress);

// Your own position should land at the centre of the screen.
if (auto pos = WorldToScreen(localPlayerPos, vp, Draw::ScreenSize())) {
    Draw::CircleFilled(*pos, 6.0f, Color::Red());
}
```

If the dot sits on your player, the matrix is right.

**If it's mirrored, sheared, or tracking sideways, switch the layout:**

```cpp
WorldToScreen(pos, vp, screen, MatrixLayout::ColumnMajor);
```

Engines disagree on storage order. Try `RowMajor` first; if it's wrong, the
other one is right. This is the single most common ESP bug and it is a one-line
fix — do not go auditing your entity loop first.

---

## 3. Finding the entity list

Entities are usually stored as either a flat array of pointers or a linked list.

### Cheat Engine approach

1. Find something entity-specific first — your own health is easiest. Scan for
   the value, take damage, scan the new value, repeat.
2. Right-click the health address → **Find out what accesses this address**.
3. Play until the list populates, then look at the instructions. One will be
   reading through a base pointer at a fixed offset.
4. Follow that base pointer upward with **Find out what accesses** repeatedly
   until you reach an address inside a module (green in Cheat Engine).

That module-relative address is your entity list. Note it as `module base +
offset`, never as an absolute address — the base changes every run under ASLR.

### Reading a flat array

```cpp
const Address listBase = moduleBase + kEntityListOffset;

for (int i = 0; i < kMaxEntities; ++i) {
    const Address entity = Memory::Read<Address>(listBase + i * sizeof(Address));
    if (!entity || !Memory::IsReadable(entity, 0x100))
        continue;

    const Vec3 pos = Memory::Read<Vec3>(entity + kPositionOffset);
    // ...
}
```

Validate every pointer. A list with 64 slots usually has most of them null or
stale, and dereferencing one is an instant crash.

### Reading a linked list

```cpp
Address node = Memory::Read<Address>(moduleBase + kListHeadOffset);
int guard = 0;

while (node && Memory::IsReadable(node, 0x100) && guard++ < 4096) {
    const Vec3 pos = Memory::Read<Vec3>(node + kPositionOffset);
    // ...
    node = Memory::Read<Address>(node + kNextOffset);
}
```

The `guard` counter matters. A corrupt or circular list will hang the render
thread forever, which looks exactly like the game freezing.

---

## 4. Finding per-entity fields

Once you have one valid entity pointer, the rest are offsets from it.

Note the entity's address, then in Cheat Engine browse memory there and look for
recognisable values:

- **Health** — an int or float matching your HUD
- **Team** — a small int that differs between you and enemies
- **Name** — a `char[]` inline, or a pointer to one
- **Position** — three floats you can watch change as you move

Find each by scanning for the known value and cross-referencing which offset
from the entity base it lands at. Write them down as constants:

```cpp
namespace Offsets {
    constexpr Offset kEntityList = 0x1A2B3C0;   // module-relative
    constexpr Offset kViewMatrix = 0x1B4D200;   // module-relative
    constexpr Offset kLocalPlayer = 0x1A38EE0;  // module-relative

    // entity-relative
    constexpr Offset kPosition = 0x1224;
    constexpr Offset kHealth   = 0x0334;
    constexpr Offset kTeam     = 0x03BC;
    constexpr Offset kName     = 0x0630;
}
```

Keeping them in one namespace means a game update is one block to re-find rather
than a hunt through your whole codebase.

---

## 5. Making offsets survive updates

Raw offsets break on every patch. Signatures usually don't.

Instead of hardcoding where the entity list lives, find the *instruction* that
references it and resolve through that:

```cpp
// mov rax, [rip+disp32]  →  48 8B 05 xx xx xx xx
const Address site = Memory::Pattern("48 8B 05 ? ? ? ? 48 85 C0").Scan(module);
if (!site) {
    KORE_WARN("entity list signature miss");
    return;
}
const Address entityList = Memory::ResolveRelative(site, 3, 7);
```

Wildcard every operand that can shift between builds — displacements,
immediates, addresses — and keep the opcodes.

**Verify with `ScanAll` that you get exactly one hit.** More than one means the
signature is too generic and will silently latch onto the wrong function after a
patch. The example payload's Signature scanner does this for you.

Entity-relative offsets (`kPosition`, `kHealth`) usually can't be signatured
practically — accept re-finding those on each update, and keep them in one place
so it's quick.

---

## 6. Wiring it into a feature

```cpp
class EntityEsp final : public Feature {
public:
    const char* Name() const override { return "Entity ESP"; }
    const char* Category() const override { return "Visuals"; }

    void OnAttach() override {
        const auto module = Memory::Module::Find("client.dll");
        if (!module) { KORE_ERROR("client.dll not loaded"); return; }

        m_base = module->Base();

        const Address site = Memory::Pattern("48 8B 05 ? ? ? ? 48 85 C0").Scan(*module);
        if (!site) { KORE_WARN("ESP: signature miss, feature inert"); return; }

        m_entityList = Memory::ResolveRelative(site, 3, 7);
        m_ready = true;
        KORE_INFO("ESP ready, entity list at {:#x}", m_entityList);
    }

    void OnRender() override {
        using namespace Render;

        if (!m_ready || !Draw::Ready())
            return;

        const Vec2 screen = Draw::ScreenSize();
        const auto vp = Memory::Read<Matrix4x4>(m_base + Offsets::kViewMatrix);

        const Address local = Memory::Read<Address>(m_base + Offsets::kLocalPlayer);
        if (!Memory::IsReadable(local, 0x100))
            return;
        const Vec3 eye = Memory::Read<Vec3>(local + Offsets::kPosition);

        for (int i = 0; i < kMaxEntities; ++i) {
            const Address entity =
                Memory::Read<Address>(m_entityList + i * sizeof(Address));

            if (!entity || entity == local || !Memory::IsReadable(entity, 0x100))
                continue;

            const int health = Memory::Read<int>(entity + Offsets::kHealth);
            if (health <= 0 || health > 100)     // also filters garbage reads
                continue;

            const Vec3 pos = Memory::Read<Vec3>(entity + Offsets::kPosition);
            if (!pos.IsFinite())
                continue;

            const float distance = eye.Distance(pos);
            if (distance > m_maxDistance)
                continue;

            const auto box = Draw::ProjectBounds(pos, kExtents, vp);
            if (!box)                            // behind the camera
                continue;

            const auto& [min, max] = *box;
            const Color color = Color::Lerp(Color::Red(), Color::Green(),
                                            health / 100.0f);

            if (m_boxes)  Draw::CornerBox(min, max, color, 1.5f);
            if (m_health) Draw::HealthBar(min, max, health / 100.0f);
            if (m_dist)   Draw::TextCentered({(min.x + max.x) * 0.5f, max.y + 3.0f},
                                             std::format("{:.0f}m", distance),
                                             Color::White());
        }
    }

    void OnMenu() override {
        if (!Enabled()) return;
        if (!m_ready) { ImGui::TextColored({1,0.4f,0.4f,1}, "Signature miss"); return; }
        ImGui::Checkbox("Boxes", &m_boxes);
        ImGui::Checkbox("Health bars", &m_health);
        ImGui::Checkbox("Distance", &m_dist);
        ImGui::SliderFloat("Max distance", &m_maxDistance, 10.0f, 500.0f);
    }

private:
    static constexpr int  kMaxEntities = 64;
    static constexpr Vec3 kExtents{0.5f, 0.5f, 1.8f};

    Address m_base = 0, m_entityList = 0;
    bool m_ready = false, m_boxes = true, m_health = true, m_dist = true;
    float m_maxDistance = 300.0f;
};
```

Note the shape:

- `OnAttach` resolves addresses **once** and sets `m_ready`. A signature miss
  leaves the feature inert instead of reading address zero.
- `OnRender` validates every pointer before dereferencing it.
- The health sanity check (`> 100`) also catches garbage from a stale slot.
- `ProjectBounds` returning `nullopt` is a `continue`, never a fallback value.

---

## 7. Choosing box extents

`kExtents` is **half-widths** in the game's world units, so a 1.8 z-extent is a
3.6-unit-tall entity.

Games differ in scale — some use metres, some use inches, some something
arbitrary. If your boxes are wildly too big or small, that's the units, not your
projection. Tune by eye against a standing entity: the box should just contain
it.

If entities have varying sizes, read the model's bounding box from the entity
rather than hardcoding.

---

## 8. Order of work

1. **Matrix first.** Project your own position, confirm the dot sits centre-screen.
2. **One entity.** Get a single valid pointer and draw a dot at its position.
3. **The list.** Loop, with validation, and draw dots for all of them.
4. **Boxes.** Swap dots for `ProjectBounds` + `CornerBox`.
5. **Extras.** Health, names, distance, tracers.
6. **Signatures.** Replace hardcoded offsets once it all works.

Don't skip to step 4. If boxes are wrong and you never verified step 1, you
can't tell whether the matrix, the offsets, or the extents are at fault.

---

## 9. Failure modes

| Symptom | Cause |
|---|---|
| Boxes mirrored, sheared, or tracking sideways | Wrong `MatrixLayout` — switch it |
| Boxes piled at a screen corner or on your player | Not handling `nullopt` from `WorldToScreen` |
| Boxes in roughly the right place but wrong size | `kExtents` in the wrong units |
| Boxes lag behind the game by a frame | Caching the matrix; read it every frame |
| Random crashes after a few minutes | Cached entity pointer — the game freed it. Validate every frame |
| Crash immediately on load | Reading a null/miss address; check for a signature miss in the log |
| Works, then breaks after a game update | Expected. Re-find the offsets; signature more of them this time |
| Game freezes | Unbounded linked-list walk — add the guard counter |

When something looks wrong, check the log first: it records every signature miss
by name.

---

## 10. Scope

This library is reverse-engineering and modding tooling — trainers, debug
overlays, single-player mods. It contains no anti-cheat evasion and isn't built
to have any. What a given game's terms permit, particularly online, is yours to
check.

---

## See also

- [API reference](api-reference.md) — every call explained
- [Memory](memory.md) — scanning, patching, pointer chains
- [Rendering](rendering.md) — overlay, backends, drawing
- [Troubleshooting](troubleshooting.md)
