#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace outlineperf {

inline constexpr std::uint64_t kMaskRefreshIntervalMs = 33;
inline constexpr std::size_t kCursorHitQuerySlotCount = 2;

struct CursorPixelStability {
    bool Observe(int pixelX, int pixelY) {
        if (!initialized || x != pixelX || y != pixelY) {
            initialized = true;
            x = pixelX;
            y = pixelY;
            return false;
        }
        return true;
    }

    void Reset() {
        initialized = false;
        x = 0;
        y = 0;
    }

    bool initialized = false;
    int x = 0;
    int y = 0;
};

struct CursorQuerySession {
    [[nodiscard]] std::uint64_t Current() const noexcept {
        return current_;
    }

    [[nodiscard]] bool IsCurrent(std::uint64_t session) const noexcept {
        return session == current_;
    }

    void Retire() noexcept {
        ++current_;
        if (current_ == 0) {
            current_ = 1;
        }
    }

private:
    std::uint64_t current_ = 1;
};

struct ProjectedPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct ScreenRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct ScreenRoi {
    ScreenRect capture{};
    ScreenRect composite{};
};

inline bool ContainsPixel(const ScreenRect& rect, int x, int y) {
    return x >= rect.left
        && x < rect.right
        && y >= rect.top
        && y < rect.bottom;
}

inline std::uint64_t Area(const ScreenRect& rect) {
    const int width = std::max(0, rect.right - rect.left);
    const int height = std::max(0, rect.bottom - rect.top);
    return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
}

inline bool TryBuildScreenRoiFromBounds(
    double minX,
    double minY,
    double maxX,
    double maxY,
    unsigned int viewportWidth,
    unsigned int viewportHeight,
    int outlineThickness,
    ScreenRoi& result) {
    result = {};
    if (!std::isfinite(minX)
        || !std::isfinite(minY)
        || !std::isfinite(maxX)
        || !std::isfinite(maxY)
        || minX > maxX
        || minY > maxY
        || viewportWidth == 0
        || viewportHeight == 0
        || viewportWidth > static_cast<unsigned int>(std::numeric_limits<int>::max())
        || viewportHeight > static_cast<unsigned int>(std::numeric_limits<int>::max())
        || outlineThickness < 1) {
        return false;
    }

    const auto buildRect = [&](int padding, ScreenRect& rect) {
        const int width = static_cast<int>(viewportWidth);
        const int height = static_cast<int>(viewportHeight);
        const auto clampToInt = [](double value, int maximum) {
            return static_cast<int>(std::clamp(value, 0.0, static_cast<double>(maximum)));
        };
        rect.left = clampToInt(std::floor(minX) - padding, width);
        rect.top = clampToInt(std::floor(minY) - padding, height);
        rect.right = clampToInt(std::ceil(maxX) + padding, width);
        rect.bottom = clampToInt(std::ceil(maxY) + padding, height);
        return rect.right > rect.left && rect.bottom > rect.top;
    };

    const int compositePadding = outlineThickness + 1;
    const int capturePadding = compositePadding + outlineThickness;
    return buildRect(compositePadding, result.composite)
        && buildRect(capturePadding, result.capture);
}

inline bool TryBuildExpandedBoneRoi(
    double minX,
    double minY,
    double maxX,
    double maxY,
    unsigned int viewportWidth,
    unsigned int viewportHeight,
    int outlineThickness,
    ScreenRoi& result) {
    result = {};
    if (!std::isfinite(minX)
        || !std::isfinite(minY)
        || !std::isfinite(maxX)
        || !std::isfinite(maxY)
        || minX > maxX
        || minY > maxY) {
        return false;
    }

    const double width = maxX - minX;
    const double height = maxY - minY;
    if (width <= 0.0 || height <= 0.0) {
        return false;
    }

    // Skeleton matrices exclude skin volume, clothing and most attached model
    // geometry. Keep a proportional conservative margin while remaining far
    // tighter than the old 4.5 x 4.5 x 5.0 metre fallback box.
    const double horizontalMargin = std::max(12.0, width * 0.50);
    const double verticalMargin = std::max(8.0, height * 0.15);
    return TryBuildScreenRoiFromBounds(
        minX - horizontalMargin,
        minY - verticalMargin,
        maxX + horizontalMargin,
        maxY + verticalMargin,
        viewportWidth,
        viewportHeight,
        outlineThickness,
        result);
}

inline bool TryBuildScreenRoi(
    const ProjectedPoint* points,
    std::size_t pointCount,
    unsigned int viewportWidth,
    unsigned int viewportHeight,
    int outlineThickness,
    ScreenRoi& result) {
    result = {};
    if (!points || pointCount == 0) {
        return false;
    }

    float minX = points[0].x;
    float minY = points[0].y;
    float maxX = points[0].x;
    float maxY = points[0].y;
    if (!std::isfinite(minX) || !std::isfinite(minY)) {
        return false;
    }

    for (std::size_t index = 1; index < pointCount; ++index) {
        if (!std::isfinite(points[index].x) || !std::isfinite(points[index].y)) {
            return false;
        }
        minX = std::min(minX, points[index].x);
        minY = std::min(minY, points[index].y);
        maxX = std::max(maxX, points[index].x);
        maxY = std::max(maxY, points[index].y);
    }

    return TryBuildScreenRoiFromBounds(
        static_cast<double>(minX),
        static_cast<double>(minY),
        static_cast<double>(maxX),
        static_cast<double>(maxY),
        viewportWidth,
        viewportHeight,
        outlineThickness,
        result);
}

inline bool TryBuildProjectedSphereRoi(
    const ProjectedPoint& center,
    float cameraDepth,
    float screenScaleX,
    float screenScaleY,
    float worldRadius,
    unsigned int viewportWidth,
    unsigned int viewportHeight,
    int outlineThickness,
    ScreenRoi& result) {
    result = {};
    if (!std::isfinite(center.x)
        || !std::isfinite(center.y)
        || !std::isfinite(cameraDepth)
        || !std::isfinite(screenScaleX)
        || !std::isfinite(screenScaleY)
        || !std::isfinite(worldRadius)
        || cameraDepth <= 0.0f
        || screenScaleX == 0.0f
        || screenScaleY == 0.0f
        || worldRadius <= 0.0f
        || worldRadius >= cameraDepth
        || viewportWidth == 0
        || viewportHeight == 0) {
        return false;
    }

    const double depth = static_cast<double>(cameraDepth);
    const double radius = static_cast<double>(worldRadius);
    const double nearDepth = depth - radius;
    const double viewportCenterX = static_cast<double>(viewportWidth) * 0.5;
    const double viewportCenterY = static_cast<double>(viewportHeight) * 0.5;
    const double centerX = static_cast<double>(center.x);
    const double centerY = static_cast<double>(center.y);

    // Bound the camera-space box enclosing the sphere. The center-offset term
    // also covers the screen shift caused by depth variation away from the optical axis.
    const double radiusX = radius
        * (std::fabs(static_cast<double>(screenScaleX)) * depth
            + std::fabs(centerX - viewportCenterX))
        / nearDepth;
    const double radiusY = radius
        * (std::fabs(static_cast<double>(screenScaleY)) * depth
            + std::fabs(centerY - viewportCenterY))
        / nearDepth;
    if (!std::isfinite(radiusX)
        || !std::isfinite(radiusY)
        || radiusX <= 0.0
        || radiusY <= 0.0) {
        return false;
    }

    return TryBuildScreenRoiFromBounds(
        centerX - radiusX,
        centerY - radiusY,
        centerX + radiusX,
        centerY + radiusY,
        viewportWidth,
        viewportHeight,
        outlineThickness,
        result);
}

inline bool ShouldRefreshMask(
    std::uint64_t nowMs,
    std::uint64_t lastCaptureAtMs,
    bool maskValid,
    bool sameTarget,
    std::uint64_t intervalMs) {
    if (!maskValid || !sameTarget || nowMs < lastCaptureAtMs) {
        return true;
    }
    return nowMs - lastCaptureAtMs >= intervalMs;
}

} // namespace outlineperf
