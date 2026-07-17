#include "pureview/core.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void Expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void ExpectNear(double actual, double expected, double tolerance, std::string_view message) {
    Expect(std::abs(actual - expected) <= tolerance, message);
}

} // namespace

int main() {
    using namespace pureview;
    const Rect screen{0, 0, 2560, 1415};

    {
        const Rect previous{620, 180, 1100, 1100};
        const Rect next = PlanWindowFrame(
            {2560, 1440}, 1.0, screen, previous, true);
        ExpectNear(next.x, previous.x, 0.5, "landscape switch preserves left");
        ExpectNear(next.y, previous.y, 0.5, "landscape switch preserves top");
        ExpectNear(next.width / next.height, 2560.0 / 1440.0, 0.002, "landscape ratio");
    }

    {
        const Rect previous{420, 360, 1400, 788};
        const Rect next = PlanWindowFrame(
            {2000, 3000}, 1.0, screen, previous, true);
        ExpectNear(next.x, previous.x, 0.5, "portrait switch preserves left");
        ExpectNear(next.y, previous.y, 0.5, "portrait switch preserves top");
        ExpectNear(next.width / next.height, 2.0 / 3.0, 0.002, "portrait ratio");
    }

    {
        const Rect previous{2100, 900, 320, 320};
        const Rect next = PlanWindowFrame(
            {3840, 2160}, 1.0, screen, previous, true);
        ExpectNear(next.x, previous.x, 0.5, "edge switch preserves left");
        ExpectNear(next.y, previous.y, 0.5, "edge switch preserves top");
        Expect(next.right() <= screen.right() + 0.5, "edge switch fits right");
        Expect(next.bottom() <= screen.bottom() + 0.5, "edge switch fits bottom");
    }

    {
        const Rect first = PlanWindowFrame(
            {1600, 900}, 1.0, screen, std::nullopt, false);
        ExpectNear(first.x + first.width / 2.0, 1280.0, 0.5, "first image centered x");
        ExpectNear(first.y + first.height / 2.0, 707.5, 0.5, "first image centered y");
    }

    {
        ScrollIntentResolver resolver;
        const auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(1));
        Expect(
            resolver.Resolve(120, false, now).kind == ScrollActionKind::PreviousImage,
            "wheel up navigates previous");
        Expect(
            resolver.Resolve(-120, false, now + std::chrono::milliseconds(50)).kind
                == ScrollActionKind::None,
            "burst is debounced");
        Expect(
            resolver.Resolve(-120, false, now + std::chrono::milliseconds(81)).kind
                == ScrollActionKind::NextImage,
            "wheel navigation resumes after fast debounce");
        const auto zoomIn = resolver.Resolve(120, true, now);
        const auto zoomOut = resolver.Resolve(-120, true, now);
        Expect(zoomIn.kind == ScrollActionKind::Zoom && zoomIn.zoomMultiplier > 1.0, "control wheel zooms in");
        Expect(zoomOut.kind == ScrollActionKind::Zoom && zoomOut.zoomMultiplier < 1.0, "control wheel zooms out");
    }

    Expect(PreviewMaxPixelSize({2560, 1440}) == 4096, "QHD preview sizing");
    const std::uint64_t gibibyte = 1024ULL * 1024ULL * 1024ULL;
    Expect(CacheLimitBytes(16 * gibibyte) == 819ULL * 1024ULL * 1024ULL, "16 GiB cache sizing");
    Expect(CacheLimitBytes(4 * gibibyte) == 384ULL * 1024ULL * 1024ULL, "cache lower bound");
    Expect(CacheLimitBytes(64 * gibibyte) == 1024ULL * 1024ULL * 1024ULL, "cache upper bound");
    Expect(
        PrefetchIndices(5, 20, 1) == std::vector<int>({6, 7, 8, 9, 4, 3}),
        "forward prefetch");
    Expect(
        PrefetchIndices(5, 20, -1) == std::vector<int>({4, 3, 2, 1, 6, 7}),
        "reverse prefetch");
    Expect(
        PrefetchIndices(0, 3, -1) == std::vector<int>({1, 2}),
        "edge prefetch");

    if (failures == 0) {
        std::cout << "PureView core tests passed\n";
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
