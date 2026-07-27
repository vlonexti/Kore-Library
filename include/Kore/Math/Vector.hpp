#pragma once

#include <Kore/Core/Types.hpp>

#include <cmath>

namespace Kore {

/// Minimal vector types, laid out to match what games actually store in memory:
/// three or two contiguous floats, no padding, trivially copyable. That means
/// `Memory::Read<Vec3>(addr)` reads a game's position field directly.

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }
    constexpr Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }
    constexpr Vec2 operator*(float s) const { return { x * s, y * s }; }
    constexpr Vec2 operator/(float s) const { return { x / s, y / s }; }

    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }

    constexpr bool operator==(const Vec2&) const = default;

    [[nodiscard]] float LengthSq() const { return x * x + y * y; }
    [[nodiscard]] float Length() const { return std::sqrt(LengthSq()); }
    [[nodiscard]] float Distance(const Vec2& o) const { return (*this - o).Length(); }

    [[nodiscard]] Vec2 Normalized() const {
        const float len = Length();
        return len > 0.0f ? *this / len : Vec2{};
    }
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    constexpr Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    constexpr Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
    constexpr Vec3 operator/(float s) const { return { x / s, y / s, z / s }; }
    constexpr Vec3 operator-() const { return { -x, -y, -z }; }

    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }

    constexpr bool operator==(const Vec3&) const = default;

    [[nodiscard]] float LengthSq() const { return x * x + y * y + z * z; }
    [[nodiscard]] float Length() const { return std::sqrt(LengthSq()); }

    /// Prefer this over Distance() when you only need to compare or sort —
    /// it skips the square root, which matters when you run it over every
    /// entity every frame.
    [[nodiscard]] float DistanceSq(const Vec3& o) const { return (*this - o).LengthSq(); }
    [[nodiscard]] float Distance(const Vec3& o) const { return (*this - o).Length(); }

    [[nodiscard]] float Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }

    [[nodiscard]] Vec3 Cross(const Vec3& o) const {
        return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x };
    }

    [[nodiscard]] Vec3 Normalized() const {
        const float len = Length();
        return len > 0.0f ? *this / len : Vec3{};
    }

    [[nodiscard]] bool IsFinite() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    constexpr bool operator==(const Vec4&) const = default;
};

static_assert(sizeof(Vec2) == 8,  "Vec2 must match a game's two contiguous floats");
static_assert(sizeof(Vec3) == 12, "Vec3 must match a game's three contiguous floats");
static_assert(sizeof(Vec4) == 16, "Vec4 must match a game's four contiguous floats");

} // namespace Kore
