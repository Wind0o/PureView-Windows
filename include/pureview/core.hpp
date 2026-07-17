#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace pureview {

struct Size {
    double width = 0.0;
    double height = 0.0;
};

struct Rect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    [[nodiscard]] double right() const noexcept { return x + width; }
    [[nodiscard]] double bottom() const noexcept { return y + height; }
};

[[nodiscard]] Rect PlanWindowFrame(
    Size pixelSize,
    double backingScale,
    Rect screenWorkArea,
    std::optional<Rect> previousFrame,
    bool preserveTopLeft);

enum class ScrollActionKind {
    None,
    PreviousImage,
    NextImage,
    Zoom,
};

struct ScrollAction {
    ScrollActionKind kind = ScrollActionKind::None;
    double zoomMultiplier = 1.0;
};

class ScrollIntentResolver {
public:
    [[nodiscard]] ScrollAction Resolve(
        double wheelDelta,
        bool controlPressed,
        std::chrono::steady_clock::time_point timestamp);

    void Reset() noexcept;

private:
    double accumulatedDelta_ = 0.0;
    std::chrono::steady_clock::time_point lastNavigation_{};
    bool hasNavigated_ = false;
};

[[nodiscard]] int PreviewMaxPixelSize(Size screenPixelSize) noexcept;
[[nodiscard]] std::size_t CacheLimitBytes(std::uint64_t physicalMemoryBytes) noexcept;
[[nodiscard]] std::vector<int> PrefetchIndices(
    int current,
    int count,
    int direction);

} // namespace pureview
