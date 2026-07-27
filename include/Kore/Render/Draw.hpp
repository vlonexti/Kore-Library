#pragma once

#include <Kore/Core/Types.hpp>
#include <Kore/Math/Matrix.hpp>
#include <Kore/Math/Vector.hpp>

#include <optional>
#include <string_view>
#include <utility>

namespace Kore::Render {

/// RGBA colour, 8 bits per channel.
struct Color {
    u8 r = 255, g = 255, b = 255, a = 255;

    constexpr Color() = default;
    constexpr Color(u8 r_, u8 g_, u8 b_, u8 a_ = 255) : r(r_), g(g_), b(b_), a(a_) {}

    /// Same colour at a different opacity — the usual way to derive a fill
    /// from an outline.
    [[nodiscard]] constexpr Color WithAlpha(u8 alpha) const { return { r, g, b, alpha }; }

    /// Packed as ImGui expects (0xAABBGGRR).
    [[nodiscard]] constexpr u32 Packed() const {
        return (static_cast<u32>(a) << 24) | (static_cast<u32>(b) << 16) |
               (static_cast<u32>(g) << 8)  |  static_cast<u32>(r);
    }

    static constexpr Color White()  { return { 255, 255, 255 }; }
    static constexpr Color Black()  { return { 0, 0, 0 }; }
    static constexpr Color Red()    { return { 235, 64, 64 }; }
    static constexpr Color Green()  { return { 80, 220, 100 }; }
    static constexpr Color Blue()   { return { 90, 160, 255 }; }
    static constexpr Color Yellow() { return { 250, 210, 70 }; }
    static constexpr Color Orange() { return { 250, 150, 60 }; }
    static constexpr Color Cyan()   { return { 80, 220, 240 }; }

    /// Blend from `a` to `b`. Handy for health colouring: Lerp(Red, Green, hp).
    [[nodiscard]] static Color Lerp(const Color& from, const Color& to, float t);
};

enum class TextStyle {
    Plain,
    /// One-pixel dark drop shadow. Cheap, readable on most backgrounds.
    Shadowed,
    /// Dark outline on all four sides. Costs four extra draws per string but
    /// stays legible on anything, including white snow and bright skyboxes.
    Outlined,
};

/// Immediate-mode drawing onto the game's frame.
///
/// Everything here targets ImGui's *foreground* draw list, so it renders above
/// every window including the menu — which is what you want for ESP. Call these
/// from a feature's OnRender().
///
/// All coordinates are screen pixels, origin top-left, matching what
/// Kore::WorldToScreen returns.
namespace Draw {

/// True when there is a live frame to draw into. Every function below is a
/// no-op otherwise, so you don't have to guard each call.
[[nodiscard]] bool Ready();

/// The current viewport size in pixels — pass this to WorldToScreen.
[[nodiscard]] Vec2 ScreenSize();

/// Centre of the screen. Where a tracer originates, and where a crosshair goes.
[[nodiscard]] Vec2 ScreenCenter();

void Line(const Vec2& from, const Vec2& to, const Color& color, float thickness = 1.0f);

void Rect(const Vec2& min, const Vec2& max, const Color& color,
          float thickness = 1.0f, float rounding = 0.0f);

void RectFilled(const Vec2& min, const Vec2& max, const Color& color, float rounding = 0.0f);

/// Box drawn as four corner brackets rather than a full rectangle. Reads more
/// clearly than a solid box when the scene is busy.
/// `fraction` is how much of each edge the bracket covers, 0..0.5.
void CornerBox(const Vec2& min, const Vec2& max, const Color& color,
               float thickness = 1.0f, float fraction = 0.25f);

/// A box with a dark 1px outline on both sides of the stroke, so it stays
/// visible against any background.
void OutlinedBox(const Vec2& min, const Vec2& max, const Color& color, float thickness = 1.0f);

void Circle(const Vec2& center, float radius, const Color& color,
            float thickness = 1.0f, int segments = 0);

void CircleFilled(const Vec2& center, float radius, const Color& color, int segments = 0);

void Triangle(const Vec2& a, const Vec2& b, const Vec2& c, const Color& color, float thickness = 1.0f);

void TriangleFilled(const Vec2& a, const Vec2& b, const Vec2& c, const Color& color);

void Text(const Vec2& position, std::string_view text, const Color& color,
          TextStyle style = TextStyle::Shadowed, float size = 0.0f);

/// Same as Text, but horizontally centred on `position`. This is what you want
/// above a box.
void TextCentered(const Vec2& position, std::string_view text, const Color& color,
                  TextStyle style = TextStyle::Shadowed, float size = 0.0f);

/// Measure a string without drawing it.
[[nodiscard]] Vec2 TextSize(std::string_view text, float size = 0.0f);

/// Vertical health bar down the left edge of a box, coloured red-to-green.
/// `fraction` is 0..1; values outside that range are clamped.
void HealthBar(const Vec2& boxMin, const Vec2& boxMax, float fraction, float width = 4.0f);

/// Horizontal bar, for anything that isn't health — armour, cooldowns, a
/// progress readout.
void ProgressBar(const Vec2& min, const Vec2& max, float fraction,
                 const Color& fill, const Color& background = Color(0, 0, 0, 160));

/// Line from a screen position to the target. `from` defaults to the bottom
/// centre of the screen when passed a zero vector.
void Tracer(const Vec2& to, const Color& color, float thickness = 1.0f, const Vec2& from = {});

/// Screen-space bounding rectangle of a world-space box.
///
/// This is the workhorse for entity ESP: give it a position and half-extents,
/// get back a 2D box that tightly fits the entity at any angle. Returns nothing
/// if the box is entirely behind the camera.
///
///     const Vec3 extents{0.5f, 0.5f, 1.8f};  // roughly a humanoid
///     if (auto box = Draw::ProjectBounds(pos, extents, viewProjection)) {
///         Draw::CornerBox(box->first, box->second, Color::Green());
///         Draw::HealthBar(box->first, box->second, hp / maxHp);
///     }
[[nodiscard]] std::optional<std::pair<Vec2, Vec2>> ProjectBounds(
    const Vec3& origin, const Vec3& extents, const Matrix4x4& viewProjection,
    MatrixLayout layout = MatrixLayout::RowMajor);

/// The same box drawn as a true 3D wireframe — 12 edges in perspective rather
/// than a flat rectangle. Costs more draws and reads as busier; use it when the
/// entity's orientation matters. Returns false if any corner is behind the
/// camera.
bool Box3D(const Vec3& origin, const Vec3& extents, const Matrix4x4& viewProjection,
           const Color& color, float thickness = 1.0f,
           MatrixLayout layout = MatrixLayout::RowMajor);

/// Build the eight corners of an axis-aligned box centred on `origin` with
/// half-widths `extents`.
void MakeBoxCorners(const Vec3& origin, const Vec3& extents, Vec3 (&out)[8]);

} // namespace Draw
} // namespace Kore::Render
