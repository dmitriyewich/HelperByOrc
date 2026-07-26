#pragma once

#include <algorithm>
#include <cmath>

namespace targetselectormath {

struct CameraPlanePoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct WorldPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct WorldRay {
    WorldPoint origin{};
    WorldPoint target{};
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

inline bool TryIntersectRaySphere(
    const WorldRay& ray,
    const WorldPoint& center,
    float radius,
    float& distance) noexcept {
    distance = 0.0f;
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
    const float alongRay = Dot(offset, segment);
    const float outsideDistance = Dot(offset, offset) - radius * radius;
    if (!std::isfinite(alongRay)
        || !std::isfinite(outsideDistance)
        || (outsideDistance > 0.0f && alongRay > 0.0f)) {
        return false;
    }

    const float discriminant = alongRay * alongRay - outsideDistance;
    if (!std::isfinite(discriminant) || discriminant < 0.0f) {
        return false;
    }

    distance = std::max(0.0f, -alongRay - std::sqrt(discriminant));
    return std::isfinite(distance) && distance <= segmentLength;
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
