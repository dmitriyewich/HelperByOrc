#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace targetselectormath {

struct CameraPlanePoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct ScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct ScreenSegment {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct WorldPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct ScreenProjection {
    WorldPoint right{};
    WorldPoint up{};
    WorldPoint forward{};
    WorldPoint position{};
    float centerX = 0.0f;
    float centerY = 0.0f;
    float scaleX = 0.0f;
    float scaleY = 0.0f;
};

struct WorldRay {
    WorldPoint origin{};
    WorldPoint target{};
};

struct RaySphereHit {
    float entryDistance = 0.0f;
    float centerlineAlignment = 0.0f;
};

class PreparedWorldRay {
public:
    PreparedWorldRay() = default;

private:
    friend bool TryPrepareWorldRay(const WorldRay&, PreparedWorldRay&) noexcept;
    friend bool TryIntersectSelectionBounds(
        const PreparedWorldRay&, const WorldPoint&, RaySphereHit&) noexcept;

    // Only the factory can set a finite, normalized direction, once per resolve.
    WorldPoint origin_{};
    WorldPoint direction_{};
    float length_ = 0.0f;
};

inline bool IsFinite(const WorldPoint& point) noexcept {
    return std::isfinite(point.x)
        && std::isfinite(point.y)
        && std::isfinite(point.z);
}

inline float MagnitudeSquared(const WorldPoint& point) noexcept {
    return point.x * point.x + point.y * point.y + point.z * point.z;
}

inline float Dot(const WorldPoint& left, const WorldPoint& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

inline bool IsFinite(const ScreenPoint& point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

inline bool TryScoreScreenPose(
    const ScreenPoint& cursor,
    const ScreenPoint* points,
    std::size_t pointCount,
    const ScreenSegment* segments,
    std::size_t segmentCount,
    float& squaredDistance) noexcept {
    squaredDistance = 0.0f;
    if (!IsFinite(cursor)
        || !points
        || pointCount == 0
        || !segments
        || segmentCount == 0) {
        return false;
    }

    float best = std::numeric_limits<float>::max();
    for (std::size_t index = 0; index < segmentCount; ++index) {
        const ScreenSegment& segment = segments[index];
        if (segment.begin >= pointCount || segment.end >= pointCount) {
            return false;
        }

        const ScreenPoint& begin = points[segment.begin];
        const ScreenPoint& end = points[segment.end];
        if (!IsFinite(begin) || !IsFinite(end)) {
            return false;
        }

        const float deltaX = end.x - begin.x;
        const float deltaY = end.y - begin.y;
        const float lengthSquared = deltaX * deltaX + deltaY * deltaY;
        if (!std::isfinite(lengthSquared)) {
            return false;
        }

        float ratio = 0.0f;
        constexpr float kSegmentEpsilon = 0.000001f;
        if (lengthSquared > kSegmentEpsilon) {
            ratio = std::clamp(
                ((cursor.x - begin.x) * deltaX + (cursor.y - begin.y) * deltaY)
                    / lengthSquared,
                0.0f,
                1.0f);
        }

        const float nearestX = begin.x + deltaX * ratio;
        const float nearestY = begin.y + deltaY * ratio;
        const float distanceX = cursor.x - nearestX;
        const float distanceY = cursor.y - nearestY;
        const float candidate = distanceX * distanceX + distanceY * distanceY;
        if (!std::isfinite(candidate)) {
            return false;
        }
        best = std::min(best, candidate);
    }

    if (!std::isfinite(best)) {
        return false;
    }
    squaredDistance = best;
    return true;
}

inline bool TryIntersectRaySphere(
    const WorldRay& ray,
    const WorldPoint& center,
    float radius,
    RaySphereHit& hit) noexcept {
    hit = {};
    if (!IsFinite(ray.origin)
        || !IsFinite(ray.target)
        || !IsFinite(center)
        || !std::isfinite(radius)
        || radius <= 0.0f) {
        return false;
    }

    WorldPoint segment{
        ray.target.x - ray.origin.x,
        ray.target.y - ray.origin.y,
        ray.target.z - ray.origin.z,
    };
    const float segmentLengthSquared = MagnitudeSquared(segment);
    constexpr float kDirectionEpsilon = 0.000001f;
    if (!std::isfinite(segmentLengthSquared) || segmentLengthSquared <= kDirectionEpsilon) {
        return false;
    }

    const float segmentLength = std::sqrt(segmentLengthSquared);
    const float inverseLength = 1.0f / segmentLength;
    segment.x *= inverseLength;
    segment.y *= inverseLength;
    segment.z *= inverseLength;

    const WorldPoint offset{
        ray.origin.x - center.x,
        ray.origin.y - center.y,
        ray.origin.z - center.z,
    };
    const float radiusSquared = radius * radius;
    const float alongRay = Dot(offset, segment);
    const float outsideDistance = Dot(offset, offset) - radiusSquared;
    if (!std::isfinite(alongRay)
        || !std::isfinite(outsideDistance)
        || (outsideDistance > 0.0f && alongRay > 0.0f)) {
        return false;
    }

    const float discriminant = alongRay * alongRay - outsideDistance;
    if (!std::isfinite(discriminant) || discriminant < 0.0f) {
        return false;
    }

    hit.entryDistance = std::max(0.0f, -alongRay - std::sqrt(discriminant));
    hit.centerlineAlignment = discriminant;
    return std::isfinite(hit.entryDistance) && hit.entryDistance <= segmentLength;
}

inline bool TryIntersectRaySphere(
    const WorldRay& ray,
    const WorldPoint& center,
    float radius,
    float& distance) noexcept {
    distance = 0.0f;
    RaySphereHit hit{};
    if (!TryIntersectRaySphere(ray, center, radius, hit)) {
        return false;
    }
    distance = hit.entryDistance;
    return true;
}

inline bool TryPrepareWorldRay(const WorldRay& ray, PreparedWorldRay& result) noexcept {
    result = {};
    if (!IsFinite(ray.origin) || !IsFinite(ray.target)) {
        return false;
    }
    WorldPoint direction{
        ray.target.x - ray.origin.x,
        ray.target.y - ray.origin.y,
        ray.target.z - ray.origin.z,
    };
    const float lengthSquared = MagnitudeSquared(direction);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001f) {
        return false;
    }
    const float length = std::sqrt(lengthSquared);
    const float inverseLength = 1.0f / length;
    direction.x *= inverseLength;
    direction.y *= inverseLength;
    direction.z *= inverseLength;
    result.origin_ = ray.origin;
    result.direction_ = direction;
    result.length_ = length;
    return true;
}

inline bool TryResolveSphereProjection(
    float alongRay,
    float outsideDistance,
    float segmentLength,
    RaySphereHit& hit) noexcept {
    if (!std::isfinite(alongRay) || !std::isfinite(outsideDistance)
        || (outsideDistance > 0.0f && alongRay > 0.0f)) {
        return false;
    }
    const float discriminant = alongRay * alongRay - outsideDistance;
    if (!std::isfinite(discriminant) || discriminant < 0.0f) {
        return false;
    }
    hit.entryDistance = std::max(0.0f, -alongRay - std::sqrt(discriminant));
    hit.centerlineAlignment = discriminant;
    return std::isfinite(hit.entryDistance) && hit.entryDistance <= segmentLength;
}

inline bool TryIntersectSelectionBounds(
    const PreparedWorldRay& ray,
    const WorldPoint& position,
    RaySphereHit& hit) noexcept {
    hit = {};
    if (ray.length_ <= 0.0f || !IsFinite(position)) {
        return false;
    }
    // Keep the original upper sphere and its depth tie-break unchanged.
    const WorldPoint upperOffset{
        ray.origin_.x - position.x,
        ray.origin_.y - position.y,
        ray.origin_.z - (position.z + 1.0f),
    };
    const float alongRay = Dot(upperOffset, ray.direction_);
    const float outsideDistance = MagnitudeSquared(upperOffset) - 2.25f;
    if (TryResolveSphereProjection(alongRay, outsideDistance, ray.length_, hit)) {
        return true;
    }
    // The root-centered lower sphere adds feet without removing old upper hits.
    // Share the projection work; do not normalize the ray again for each ped.
    return TryResolveSphereProjection(
        alongRay + ray.direction_.z,
        outsideDistance + 2.0f * upperOffset.z + 1.0f,
        ray.length_,
        hit);
}

inline bool IsValidCameraBasis(
    const WorldPoint& right,
    const WorldPoint& up,
    const WorldPoint& forward) noexcept {
    const float rightLengthSquared = MagnitudeSquared(right);
    const float upLengthSquared = MagnitudeSquared(up);
    const float forwardLengthSquared = MagnitudeSquared(forward);
    constexpr float kMinAxisLengthSquared = 0.5f;
    constexpr float kMaxAxisLengthSquared = 1.5f;
    if (!std::isfinite(rightLengthSquared)
        || !std::isfinite(upLengthSquared)
        || !std::isfinite(forwardLengthSquared)
        || rightLengthSquared < kMinAxisLengthSquared
        || rightLengthSquared > kMaxAxisLengthSquared
        || upLengthSquared < kMinAxisLengthSquared
        || upLengthSquared > kMaxAxisLengthSquared
        || forwardLengthSquared < kMinAxisLengthSquared
        || forwardLengthSquared > kMaxAxisLengthSquared) {
        return false;
    }

    constexpr float kMaxNormalizedDotSquared = 0.0025f;
    const float rightUpDot = Dot(right, up);
    const float rightForwardDot = Dot(right, forward);
    const float upForwardDot = Dot(up, forward);
    return rightUpDot * rightUpDot
            <= kMaxNormalizedDotSquared * rightLengthSquared * upLengthSquared
        && rightForwardDot * rightForwardDot
            <= kMaxNormalizedDotSquared * rightLengthSquared * forwardLengthSquared
        && upForwardDot * upForwardDot
            <= kMaxNormalizedDotSquared * upLengthSquared * forwardLengthSquared;
}

inline bool TryMapScreenToCameraPlane(
    float screenX,
    float screenY,
    float screenWidth,
    float screenHeight,
    float viewWindowX,
    float viewWindowY,
    CameraPlanePoint& result) noexcept {
    result = {};
    if (!std::isfinite(screenX)
        || !std::isfinite(screenY)
        || !std::isfinite(screenWidth)
        || !std::isfinite(screenHeight)
        || !std::isfinite(viewWindowX)
        || !std::isfinite(viewWindowY)
        || screenWidth <= 0.0f
        || screenHeight <= 0.0f
        || viewWindowX <= 0.0f
        || viewWindowY <= 0.0f) {
        return false;
    }

    const float normalizedX = (((screenX + 0.5f) / screenWidth) * 2.0f) - 1.0f;
    const float normalizedY = 1.0f - (((screenY + 0.5f) / screenHeight) * 2.0f);

    // GTA's screen projection maps increasing screen X against the Rw camera-frame right axis.
    result.x = -normalizedX * viewWindowX;
    result.y = normalizedY * viewWindowY;
    return std::isfinite(result.x) && std::isfinite(result.y);
}

inline bool TryProjectWorldToScreen(
    const WorldPoint& world,
    const ScreenProjection& projection,
    ScreenPoint& result,
    float& cameraDepth) noexcept {
    result = {};
    cameraDepth = 0.0f;
    if (!IsFinite(world)) {
        return false;
    }

    const WorldPoint relative{
        world.x - projection.position.x,
        world.y - projection.position.y,
        world.z - projection.position.z,
    };
    cameraDepth = Dot(relative, projection.forward);
    constexpr float kNearDepthEpsilon = 0.001f;
    if (!std::isfinite(cameraDepth) || cameraDepth <= kNearDepthEpsilon) {
        cameraDepth = 0.0f;
        return false;
    }

    const float inverseDepth = 1.0f / cameraDepth;
    result.x = projection.centerX
        + Dot(relative, projection.right) * inverseDepth * projection.scaleX;
    result.y = projection.centerY
        + Dot(relative, projection.up) * inverseDepth * projection.scaleY;
    return IsFinite(result);
}

template <typename CameraMatrix>
inline bool TryBuildScreenProjection(
    const CameraMatrix& matrix,
    float screenWidth,
    float screenHeight,
    float viewWindowX,
    float viewWindowY,
    ScreenProjection& result) noexcept {
    result = {};
    const WorldPoint right{ matrix.right.x, matrix.right.y, matrix.right.z };
    const WorldPoint up{ matrix.up.x, matrix.up.y, matrix.up.z };
    const WorldPoint forward{ matrix.at.x, matrix.at.y, matrix.at.z };
    const WorldPoint position{ matrix.pos.x, matrix.pos.y, matrix.pos.z };
    if (!IsFinite(right)
        || !IsFinite(up)
        || !IsFinite(forward)
        || !IsFinite(position)
        || !IsValidCameraBasis(right, up, forward)
        || !std::isfinite(screenWidth)
        || !std::isfinite(screenHeight)
        || !std::isfinite(viewWindowX)
        || !std::isfinite(viewWindowY)
        || screenWidth <= 0.0f
        || screenHeight <= 0.0f
        || viewWindowX <= 0.0f
        || viewWindowY <= 0.0f) {
        return false;
    }

    result.right = right;
    result.up = up;
    result.forward = forward;
    result.position = position;
    result.centerX = screenWidth * 0.5f - 0.5f;
    result.centerY = screenHeight * 0.5f - 0.5f;
    result.scaleX = -(screenWidth * 0.5f) / viewWindowX;
    result.scaleY = -(screenHeight * 0.5f) / viewWindowY;
    return std::isfinite(result.centerX)
        && std::isfinite(result.centerY)
        && std::isfinite(result.scaleX)
        && std::isfinite(result.scaleY);
}

template <typename CameraMatrix>
inline bool TryProjectWorldToScreen(
    const WorldPoint& world,
    const CameraMatrix& matrix,
    float screenWidth,
    float screenHeight,
    float viewWindowX,
    float viewWindowY,
    ScreenPoint& result,
    float& cameraDepth) noexcept {
    ScreenProjection projection{};
    return TryBuildScreenProjection(
               matrix,
               screenWidth,
               screenHeight,
               viewWindowX,
               viewWindowY,
               projection)
        && TryProjectWorldToScreen(world, projection, result, cameraDepth);
}

template <typename CameraMatrix>
inline bool TryBuildWorldRay(
    const CameraPlanePoint& cameraPlane,
    const CameraMatrix& matrix,
    float distance,
    WorldRay& result) noexcept {
    result = {};
    const WorldPoint right{ matrix.right.x, matrix.right.y, matrix.right.z };
    const WorldPoint up{ matrix.up.x, matrix.up.y, matrix.up.z };
    const WorldPoint forward{ matrix.at.x, matrix.at.y, matrix.at.z };
    const WorldPoint position{ matrix.pos.x, matrix.pos.y, matrix.pos.z };
    if (!std::isfinite(cameraPlane.x)
        || !std::isfinite(cameraPlane.y)
        || !IsFinite(right)
        || !IsFinite(up)
        || !IsFinite(forward)
        || !IsFinite(position)
        || !IsValidCameraBasis(right, up, forward)
        || !std::isfinite(distance)
        || distance <= 0.0f) {
        return false;
    }

    WorldPoint direction{
        forward.x + right.x * cameraPlane.x + up.x * cameraPlane.y,
        forward.y + right.y * cameraPlane.x + up.y * cameraPlane.y,
        forward.z + right.z * cameraPlane.x + up.z * cameraPlane.y,
    };
    const float magnitudeSquared = direction.x * direction.x
        + direction.y * direction.y
        + direction.z * direction.z;
    constexpr float kDirectionEpsilon = 0.000001f;
    if (!std::isfinite(magnitudeSquared) || magnitudeSquared <= kDirectionEpsilon) {
        return false;
    }

    const float inverseMagnitude = 1.0f / std::sqrt(magnitudeSquared);
    direction.x *= inverseMagnitude;
    direction.y *= inverseMagnitude;
    direction.z *= inverseMagnitude;
    result.origin = position;
    result.target = {
        position.x + direction.x * distance,
        position.y + direction.y * distance,
        position.z + direction.z * distance,
    };
    return IsFinite(result.target);
}

} // namespace targetselectormath
