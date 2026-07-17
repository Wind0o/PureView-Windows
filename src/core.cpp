#include "pureview/core.hpp"

#include <algorithm>
#include <cmath>

namespace pureview {
namespace {

constexpr double kMinimumWidth = 320.0;
constexpr double kMinimumHeight = 220.0;
constexpr double kMinimumAnchoredWidth = 240.0;
constexpr double kMinimumAnchoredHeight = 160.0;
constexpr auto kNavigationDebounce = std::chrono::milliseconds(80);
constexpr double kWheelDetent = 120.0;

double Clamp(double value, double minimum, double maximum) {
    if (maximum < minimum) {
        return minimum;
    }
    return std::clamp(value, minimum, maximum);
}

} // namespace

Rect PlanWindowFrame(
    Size pixelSize,
    double backingScale,
    Rect screenWorkArea,
    std::optional<Rect> previousFrame,
    bool preserveTopLeft) {
    if (pixelSize.width <= 0.0 || pixelSize.height <= 0.0) {
        if (previousFrame.has_value()) {
            return *previousFrame;
        }
        return {
            std::round(screenWorkArea.x + (screenWorkArea.width - 640.0) / 2.0),
            std::round(screenWorkArea.y + (screenWorkArea.height - 440.0) / 2.0),
            640.0,
            440.0,
        };
    }

    const double safeScale = std::max(backingScale, 1.0);
    const Size maximumSize{
        screenWorkArea.width * 0.88,
        screenWorkArea.height * 0.88,
    };
    Size size{
        pixelSize.width / safeScale,
        pixelSize.height / safeScale,
    };

    const double grow = std::max({
        1.0,
        kMinimumWidth / std::max(size.width, 1.0),
        kMinimumHeight / std::max(size.height, 1.0),
    });
    size.width *= grow;
    size.height *= grow;

    const double initialShrink = std::min({
        1.0,
        maximumSize.width / std::max(size.width, 1.0),
        maximumSize.height / std::max(size.height, 1.0),
    });
    size.width *= initialShrink;
    size.height *= initialShrink;

    if (preserveTopLeft && previousFrame.has_value()) {
        const double anchorX = Clamp(
            previousFrame->x,
            screenWorkArea.x,
            screenWorkArea.right() - kMinimumAnchoredWidth);
        const double anchorY = Clamp(
            previousFrame->y,
            screenWorkArea.y,
            screenWorkArea.bottom() - kMinimumAnchoredHeight);
        const Size available{
            std::max(1.0, screenWorkArea.right() - anchorX),
            std::max(1.0, screenWorkArea.bottom() - anchorY),
        };
        const double anchoredShrink = std::min({
            1.0,
            available.width / std::max(size.width, 1.0),
            available.height / std::max(size.height, 1.0),
        });
        size.width *= anchoredShrink;
        size.height *= anchoredShrink;
        return {
            std::round(anchorX),
            std::round(anchorY),
            std::round(size.width),
            std::round(size.height),
        };
    }

    const double roundedWidth = std::round(size.width);
    const double roundedHeight = std::round(size.height);
    return {
        std::round(screenWorkArea.x + (screenWorkArea.width - roundedWidth) / 2.0),
        std::round(screenWorkArea.y + (screenWorkArea.height - roundedHeight) / 2.0),
        roundedWidth,
        roundedHeight,
    };
}

ScrollAction ScrollIntentResolver::Resolve(
    double wheelDelta,
    bool controlPressed,
    std::chrono::steady_clock::time_point timestamp) {
    if (std::abs(wheelDelta) < 0.01) {
        return {};
    }

    if (controlPressed) {
        accumulatedDelta_ = 0.0;
        return {
            ScrollActionKind::Zoom,
            std::exp((wheelDelta / kWheelDetent) * 0.18),
        };
    }

    accumulatedDelta_ += wheelDelta;
    if (std::abs(accumulatedDelta_) < kWheelDetent * 0.45) {
        return {};
    }
    if (hasNavigated_ && timestamp - lastNavigation_ < kNavigationDebounce) {
        accumulatedDelta_ = 0.0;
        return {};
    }

    const auto kind = accumulatedDelta_ > 0.0
        ? ScrollActionKind::PreviousImage
        : ScrollActionKind::NextImage;
    accumulatedDelta_ = 0.0;
    lastNavigation_ = timestamp;
    hasNavigated_ = true;
    return {kind, 1.0};
}

void ScrollIntentResolver::Reset() noexcept {
    accumulatedDelta_ = 0.0;
    lastNavigation_ = {};
    hasNavigated_ = false;
}

int PreviewMaxPixelSize(Size screenPixelSize) noexcept {
    const double longestEdge = std::max(screenPixelSize.width, screenPixelSize.height);
    const int withZoomHeadroom = static_cast<int>(std::ceil(longestEdge * 1.6));
    const int aligned = ((withZoomHeadroom + 511) / 512) * 512;
    return std::clamp(aligned, 3072, 6144);
}

std::size_t CacheLimitBytes(std::uint64_t physicalMemoryBytes) noexcept {
    constexpr std::uint64_t mebibyte = 1024ULL * 1024ULL;
    const std::uint64_t proportional = physicalMemoryBytes / 20ULL;
    const std::uint64_t bounded = std::clamp(
        proportional,
        384ULL * mebibyte,
        1024ULL * mebibyte);
    return static_cast<std::size_t>((bounded / mebibyte) * mebibyte);
}

std::vector<int> PrefetchIndices(int current, int count, int direction) {
    if (count <= 1 || current < 0 || current >= count) {
        return {};
    }

    const int forward = direction < 0 ? -1 : 1;
    std::vector<int> result;
    result.reserve(6);
    for (int distance = 1; distance <= 4; ++distance) {
        const int index = current + distance * forward;
        if (index >= 0 && index < count) {
            result.push_back(index);
        }
    }
    for (int distance = 1; distance <= 2; ++distance) {
        const int index = current - distance * forward;
        if (index >= 0 && index < count) {
            result.push_back(index);
        }
    }
    return result;
}

} // namespace pureview
