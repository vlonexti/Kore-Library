#include <Kore/Render/Draw.hpp>
#include <Kore/Render/Overlay.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace Kore::Render {
namespace {

constexpr ImVec2 ToIm(const Vec2& v) { return ImVec2(v.x, v.y); }
constexpr Vec2 FromIm(const ImVec2& v) { return Vec2(v.x, v.y); }

/// The foreground list draws above every ImGui window, so ESP never ends up
/// behind the menu.
ImDrawList* List() {
    if (ImGui::GetCurrentContext() == nullptr)
        return nullptr;
    return ImGui::GetForegroundDrawList();
}

float ResolveSize(float requested) {
    return requested > 0.0f ? requested : ImGui::GetFontSize();
}

} // namespace

Color Color::Lerp(const Color& from, const Color& to, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const auto mix = [t](u8 a, u8 b) {
        return static_cast<u8>(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t);
    };
    return { mix(from.r, to.r), mix(from.g, to.g), mix(from.b, to.b), mix(from.a, to.a) };
}

namespace Draw {

bool Ready() {
    return ImGui::GetCurrentContext() != nullptr && Overlay::Get().ImGuiReady();
}

Vec2 ScreenSize() {
    if (!Ready())
        return {};
    return FromIm(ImGui::GetIO().DisplaySize);
}

Vec2 ScreenCenter() {
    const Vec2 size = ScreenSize();
    return { size.x * 0.5f, size.y * 0.5f };
}

void Line(const Vec2& from, const Vec2& to, const Color& color, float thickness) {
    if (ImDrawList* list = List())
        list->AddLine(ToIm(from), ToIm(to), color.Packed(), thickness);
}

void Rect(const Vec2& min, const Vec2& max, const Color& color, float thickness, float rounding) {
    if (ImDrawList* list = List())
        list->AddRect(ToIm(min), ToIm(max), color.Packed(), rounding, 0, thickness);
}

void RectFilled(const Vec2& min, const Vec2& max, const Color& color, float rounding) {
    if (ImDrawList* list = List())
        list->AddRectFilled(ToIm(min), ToIm(max), color.Packed(), rounding);
}

void OutlinedBox(const Vec2& min, const Vec2& max, const Color& color, float thickness) {
    const Color shadow = Color::Black().WithAlpha(color.a);

    // Dark stroke on both sides of the coloured one. Two extra rects, and the
    // box stays readable on white and black alike.
    Rect({ min.x - 1.0f, min.y - 1.0f }, { max.x + 1.0f, max.y + 1.0f }, shadow, thickness);
    Rect({ min.x + 1.0f, min.y + 1.0f }, { max.x - 1.0f, max.y - 1.0f }, shadow, thickness);
    Rect(min, max, color, thickness);
}

void CornerBox(const Vec2& min, const Vec2& max, const Color& color, float thickness, float fraction) {
    const float width  = max.x - min.x;
    const float height = max.y - min.y;
    if (width <= 0.0f || height <= 0.0f)
        return;

    fraction = std::clamp(fraction, 0.05f, 0.5f);
    const float dx = width * fraction;
    const float dy = height * fraction;

    // top-left
    Line(min, { min.x + dx, min.y }, color, thickness);
    Line(min, { min.x, min.y + dy }, color, thickness);
    // top-right
    Line({ max.x, min.y }, { max.x - dx, min.y }, color, thickness);
    Line({ max.x, min.y }, { max.x, min.y + dy }, color, thickness);
    // bottom-left
    Line({ min.x, max.y }, { min.x + dx, max.y }, color, thickness);
    Line({ min.x, max.y }, { min.x, max.y - dy }, color, thickness);
    // bottom-right
    Line(max, { max.x - dx, max.y }, color, thickness);
    Line(max, { max.x, max.y - dy }, color, thickness);
}

void Circle(const Vec2& center, float radius, const Color& color, float thickness, int segments) {
    if (ImDrawList* list = List())
        list->AddCircle(ToIm(center), radius, color.Packed(), segments, thickness);
}

void CircleFilled(const Vec2& center, float radius, const Color& color, int segments) {
    if (ImDrawList* list = List())
        list->AddCircleFilled(ToIm(center), radius, color.Packed(), segments);
}

void Triangle(const Vec2& a, const Vec2& b, const Vec2& c, const Color& color, float thickness) {
    if (ImDrawList* list = List())
        list->AddTriangle(ToIm(a), ToIm(b), ToIm(c), color.Packed(), thickness);
}

void TriangleFilled(const Vec2& a, const Vec2& b, const Vec2& c, const Color& color) {
    if (ImDrawList* list = List())
        list->AddTriangleFilled(ToIm(a), ToIm(b), ToIm(c), color.Packed());
}

Vec2 TextSize(std::string_view text, float size) {
    if (!Ready())
        return {};

    const std::string owned(text);
    const float fontSize = ResolveSize(size);
    const ImVec2 measured = ImGui::GetFont()->CalcTextSizeA(
        fontSize, FLT_MAX, 0.0f, owned.c_str(), owned.c_str() + owned.size());
    return FromIm(measured);
}

void Text(const Vec2& position, std::string_view text, const Color& color,
          TextStyle style, float size) {
    ImDrawList* list = List();
    if (!list || text.empty())
        return;

    const std::string owned(text);
    const char* begin = owned.c_str();
    const char* end   = begin + owned.size();

    const float fontSize = ResolveSize(size);
    ImFont* font = ImGui::GetFont();
    const u32 shadow = Color::Black().WithAlpha(color.a).Packed();

    switch (style) {
        case TextStyle::Plain:
            break;

        case TextStyle::Shadowed:
            list->AddText(font, fontSize, ToIm({ position.x + 1.0f, position.y + 1.0f }),
                          shadow, begin, end);
            break;

        case TextStyle::Outlined:
            for (const Vec2 offset : { Vec2(-1, 0), Vec2(1, 0), Vec2(0, -1), Vec2(0, 1) }) {
                list->AddText(font, fontSize,
                              ToIm({ position.x + offset.x, position.y + offset.y }),
                              shadow, begin, end);
            }
            break;
    }

    list->AddText(font, fontSize, ToIm(position), color.Packed(), begin, end);
}

void TextCentered(const Vec2& position, std::string_view text, const Color& color,
                  TextStyle style, float size) {
    const Vec2 measured = TextSize(text, size);
    Text({ position.x - measured.x * 0.5f, position.y }, text, color, style, size);
}

void HealthBar(const Vec2& boxMin, const Vec2& boxMax, float fraction, float width) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);

    const float height = boxMax.y - boxMin.y;
    if (height <= 0.0f)
        return;

    const Vec2 barMax{ boxMin.x - 2.0f, boxMax.y };
    const Vec2 barMin{ barMax.x - width, boxMin.y };

    RectFilled({ barMin.x - 1.0f, barMin.y - 1.0f }, { barMax.x + 1.0f, barMax.y + 1.0f },
               Color(0, 0, 0, 180));

    // Fills from the bottom up, so a dying entity's bar drains downward.
    const float filledTop = barMax.y - height * fraction;
    const Color fill = Color::Lerp(Color::Red(), Color::Green(), fraction);
    RectFilled({ barMin.x, filledTop }, barMax, fill);
}

void ProgressBar(const Vec2& min, const Vec2& max, float fraction,
                 const Color& fill, const Color& background) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);

    RectFilled(min, max, background);
    const float filledRight = min.x + (max.x - min.x) * fraction;
    RectFilled(min, { filledRight, max.y }, fill);
}

void Tracer(const Vec2& to, const Color& color, float thickness, const Vec2& from) {
    Vec2 origin = from;
    if (origin == Vec2{}) {
        const Vec2 size = ScreenSize();
        origin = { size.x * 0.5f, size.y };
    }
    Line(origin, to, color, thickness);
}

void MakeBoxCorners(const Vec3& origin, const Vec3& extents, Vec3 (&out)[8]) {
    // Bit-indexed so corner i's edges are the three neighbours differing by one
    // bit — that's what Box3D relies on to enumerate the 12 edges.
    for (int i = 0; i < 8; ++i) {
        out[i] = { origin.x + extents.x * ((i & 1) ? 1.0f : -1.0f),
                   origin.y + extents.y * ((i & 2) ? 1.0f : -1.0f),
                   origin.z + extents.z * ((i & 4) ? 1.0f : -1.0f) };
    }
}

std::optional<std::pair<Vec2, Vec2>> ProjectBounds(const Vec3& origin, const Vec3& extents,
                                                   const Matrix4x4& viewProjection,
                                                   MatrixLayout layout) {
    Vec3 corners[8];
    MakeBoxCorners(origin, extents, corners);

    const Vec2 screen = ScreenSize();
    if (screen.x <= 0.0f || screen.y <= 0.0f)
        return std::nullopt;

    Vec2 min{ FLT_MAX, FLT_MAX };
    Vec2 max{ -FLT_MAX, -FLT_MAX };
    bool any = false;

    for (const Vec3& corner : corners) {
        const auto projected = WorldToScreen(corner, viewProjection, screen, layout);
        if (!projected)
            continue; // corner behind the camera — the rest still bound it

        any = true;
        min.x = std::min(min.x, projected->x);
        min.y = std::min(min.y, projected->y);
        max.x = std::max(max.x, projected->x);
        max.y = std::max(max.y, projected->y);
    }

    if (!any)
        return std::nullopt;

    return std::make_pair(min, max);
}

bool Box3D(const Vec3& origin, const Vec3& extents, const Matrix4x4& viewProjection,
           const Color& color, float thickness, MatrixLayout layout) {
    Vec3 corners[8];
    MakeBoxCorners(origin, extents, corners);

    const Vec2 screen = ScreenSize();
    if (screen.x <= 0.0f || screen.y <= 0.0f)
        return false;

    Vec2 projected[8];
    for (int i = 0; i < 8; ++i) {
        const auto point = WorldToScreen(corners[i], viewProjection, screen, layout);
        if (!point)
            return false; // any corner behind the camera and the wireframe lies
        projected[i] = *point;
    }

    // Corners differing by exactly one bit are adjacent; drawing only the
    // higher-index direction gives each of the 12 edges exactly once.
    for (int i = 0; i < 8; ++i) {
        for (const int bit : { 1, 2, 4 }) {
            const int neighbour = i | bit;
            if (neighbour != i)
                Line(projected[i], projected[neighbour], color, thickness);
        }
    }

    return true;
}

} // namespace Draw
} // namespace Kore::Render
