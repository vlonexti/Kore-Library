#include <Kore/Math/Matrix.hpp>

#include <cmath>

namespace Kore {
namespace {

struct ClipCoords {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
};

ClipCoords Project(const Vec3& world, const Matrix4x4& vp, MatrixLayout layout) {
    ClipCoords clip;

    if (layout == MatrixLayout::RowMajor) {
        clip.x = vp[0]  * world.x + vp[1]  * world.y + vp[2]  * world.z + vp[3];
        clip.y = vp[4]  * world.x + vp[5]  * world.y + vp[6]  * world.z + vp[7];
        clip.w = vp[12] * world.x + vp[13] * world.y + vp[14] * world.z + vp[15];
    } else {
        clip.x = vp[0] * world.x + vp[4] * world.y + vp[8]  * world.z + vp[12];
        clip.y = vp[1] * world.x + vp[5] * world.y + vp[9]  * world.z + vp[13];
        clip.w = vp[3] * world.x + vp[7] * world.y + vp[11] * world.z + vp[15];
    }

    return clip;
}

} // namespace

float ClipW(const Vec3& world, const Matrix4x4& viewProjection, MatrixLayout layout) {
    return Project(world, viewProjection, layout).w;
}

std::optional<Vec2> WorldToScreen(const Vec3& world,
                                  const Matrix4x4& viewProjection,
                                  const Vec2& screenSize,
                                  MatrixLayout layout) {
    if (!world.IsFinite())
        return std::nullopt;

    const ClipCoords clip = Project(world, viewProjection, layout);

    // Behind the camera, or so close to the near plane that the divide would
    // blow the result up into a wild off-screen coordinate.
    if (clip.w < 0.001f)
        return std::nullopt;

    const float inverseW = 1.0f / clip.w;
    const float ndcX = clip.x * inverseW;
    const float ndcY = clip.y * inverseW;

    const float halfWidth  = screenSize.x * 0.5f;
    const float halfHeight = screenSize.y * 0.5f;

    // NDC y points up, screen y points down — hence the subtraction.
    Vec2 screen{ halfWidth + ndcX * halfWidth,
                 halfHeight - ndcY * halfHeight };

    if (!std::isfinite(screen.x) || !std::isfinite(screen.y))
        return std::nullopt;

    return screen;
}

} // namespace Kore
