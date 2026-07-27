#pragma once

#include <Kore/Core/Types.hpp>
#include <Kore/Math/Vector.hpp>

#include <optional>

namespace Kore {

/// A 4x4 float matrix as games store it: 16 contiguous floats. Read one
/// straight out of the process with `Memory::Read<Matrix4x4>(addr)`.
///
/// Engines disagree on whether the view-projection matrix is stored row-major
/// or column-major, so WorldToScreen takes the convention as a parameter rather
/// than guessing. If your ESP boxes come out mirrored or rotated, you picked
/// the wrong one — try the other.
struct Matrix4x4 {
    float m[16]{};

    constexpr float& operator[](std::size_t i) { return m[i]; }
    constexpr const float& operator[](std::size_t i) const { return m[i]; }

    /// Row-major element access: At(row, column).
    [[nodiscard]] constexpr float At(std::size_t row, std::size_t column) const {
        return m[row * 4 + column];
    }

    static constexpr Matrix4x4 Identity() {
        Matrix4x4 out;
        out.m[0] = out.m[5] = out.m[10] = out.m[15] = 1.0f;
        return out;
    }
};

static_assert(sizeof(Matrix4x4) == 64, "Matrix4x4 must be 16 contiguous floats");

enum class MatrixLayout {
    /// Row-major — Source, Unity, and most engines that expose a
    /// "view-projection" matrix directly. Try this first.
    RowMajor,
    /// Column-major — DirectX-style storage, and what you get from some
    /// engines' transposed matrices.
    ColumnMajor,
};

/// Project a world position onto the screen.
///
/// Returns nothing when the point is behind the camera, which is the case you
/// must handle: without it, off-screen entities produce mirrored boxes drawn on
/// top of the player.
///
/// `screenSize` is the game's viewport in pixels. The result is in the same
/// pixel space, origin top-left — exactly what ImGui's draw lists expect.
[[nodiscard]] std::optional<Vec2> WorldToScreen(const Vec3& world,
                                                const Matrix4x4& viewProjection,
                                                const Vec2& screenSize,
                                                MatrixLayout layout = MatrixLayout::RowMajor);

/// The raw clip-space w value for a world point. Useful when you want to reject
/// points yourself, or to scale a box by distance.
[[nodiscard]] float ClipW(const Vec3& world,
                          const Matrix4x4& viewProjection,
                          MatrixLayout layout = MatrixLayout::RowMajor);

} // namespace Kore
