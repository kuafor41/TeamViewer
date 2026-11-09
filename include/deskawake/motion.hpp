#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace deskawake {

struct Point {
    double x{};
    double y{};
};

struct TrajectoryOptions {
    std::size_t sample_count{32};
    double overshoot_fraction{0.035};
    double tremor_pixels{0.35};
    std::uint32_t seed{0xD35A9A7u};
};

[[nodiscard]] Point cubicBezier(Point p0, Point p1, Point p2, Point p3, double t) noexcept;
[[nodiscard]] double sCurve(double t) noexcept;
[[nodiscard]] double perlin1D(double x, std::uint32_t seed) noexcept;

// Generates a deterministic path. The first and final samples are exactly start and target.
[[nodiscard]] std::vector<Point> generateHumanTrajectory(
    Point start,
    Point target,
    const TrajectoryOptions& options = {}
);

} // namespace deskawake
