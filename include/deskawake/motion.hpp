#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace deskawake {
struct Point { double x{}; double y{}; };
struct TrajectoryOptions {
    std::size_t sample_count{32};
    double overshoot_fraction{0.035};
    double perlin_amplitude_pixels{0.35};
    double gaussian_sigma_pixels{0.18};
    std::uint32_t seed{0xD35A9A7u};
};
[[nodiscard]] Point cubicBezier(Point p0, Point p1, Point p2, Point p3, double t) noexcept;
// Smoothstep S-curve: 3t^2 - 2t^3, clamped to [0, 1].
[[nodiscard]] double sCurve(double t) noexcept;
[[nodiscard]] double perlin2D(double x, double y, std::uint32_t seed) noexcept;
[[nodiscard]] std::vector<Point> generateHumanTrajectory(
    Point start, Point target, const TrajectoryOptions& options = {});
} // namespace deskawake
