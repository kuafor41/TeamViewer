#include "deskawake/motion.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <random>
#include <stdexcept>

namespace deskawake {
namespace {

[[nodiscard]] double clamp01(double value) noexcept {
    return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] Point add(Point a, Point b) noexcept {
    return {a.x + b.x, a.y + b.y};
}

[[nodiscard]] Point subtract(Point a, Point b) noexcept {
    return {a.x - b.x, a.y - b.y};
}

[[nodiscard]] Point multiply(Point p, double value) noexcept {
    return {p.x * value, p.y * value};
}

[[nodiscard]] double length(Point p) noexcept {
    return std::hypot(p.x, p.y);
}

[[nodiscard]] double lerp(double a, double b, double t) noexcept {
    return a + (b - a) * t;
}

[[nodiscard]] double fade(double t) noexcept {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

} // namespace

Point cubicBezier(Point p0, Point p1, Point p2, Point p3, double t) noexcept {
    const double u = 1.0 - clamp01(t);
    const double tClamped = clamp01(t);
    const double uu = u * u;
    const double tt = tClamped * tClamped;

    const double b0 = uu * u;
    const double b1 = 3.0 * uu * tClamped;
    const double b2 = 3.0 * u * tt;
    const double b3 = tt * tClamped;

    return {
        b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
        b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y
    };
}

double sCurve(double t) noexcept {
    const double value = clamp01(t);
    return value * value * (3.0 - 2.0 * value);
}

double perlin1D(double x, std::uint32_t seed) noexcept {
    if (!std::isfinite(x)) {
        return 0.0;
    }

    std::array<int, 256> permutation{};
    std::iota(permutation.begin(), permutation.end(), 0);
    std::mt19937 generator(seed);
    std::shuffle(permutation.begin(), permutation.end(), generator);

    const double floorValue = std::floor(x);
    const auto lattice = static_cast<int>(floorValue);
    const double local = x - floorValue;
    const int leftIndex = lattice & 255;
    const int rightIndex = (leftIndex + 1) & 255;
    const double leftGradient =
        (permutation[static_cast<std::size_t>(leftIndex)] & 1) == 0 ? 1.0 : -1.0;
    const double rightGradient =
        (permutation[static_cast<std::size_t>(rightIndex)] & 1) == 0 ? 1.0 : -1.0;

    const double left = leftGradient * local;
    const double right = rightGradient * (local - 1.0);
    return std::clamp(lerp(left, right, fade(local)) * 2.0, -1.0, 1.0);
}

std::vector<Point> generateHumanTrajectory(
    Point start,
    Point target,
    const TrajectoryOptions& options
) {
    if (options.sample_count < 2) {
        throw std::invalid_argument("sample_count must be at least 2");
    }
    if (!std::isfinite(start.x) || !std::isfinite(start.y) ||
        !std::isfinite(target.x) || !std::isfinite(target.y)) {
        throw std::invalid_argument("trajectory points must be finite");
    }

    std::vector<Point> result;
    result.reserve(options.sample_count);

    const Point displacement = subtract(target, start);
    const double distance = length(displacement);
    if (distance < 1e-9) {
        result.assign(options.sample_count, start);
        result.front() = start;
        result.back() = target;
        return result;
    }

    const Point tangent = multiply(displacement, 1.0 / distance);
    const Point normal{-tangent.y, tangent.x};
    const double overshootFraction = std::clamp(options.overshoot_fraction, 0.0, 0.20);
    const double overshootDistance = distance * overshootFraction;
    const Point overshoot = add(target, multiply(tangent, overshootDistance));

    std::mt19937 random(options.seed);
    std::uniform_real_distribution<double> bendDistribution(-0.12, 0.12);
    std::normal_distribution<double> tremorDistribution(0.0, 0.18);

    const double bend = distance * bendDistribution(random);
    const Point firstControl = add(
        add(start, multiply(tangent, distance * 0.30)),
        multiply(normal, bend)
    );
    const Point secondControl = add(
        subtract(overshoot, multiply(tangent, distance * 0.28)),
        multiply(normal, bend * 0.55)
    );
    const Point returnControl1 = subtract(
        overshoot,
        multiply(tangent, std::max(1.0, overshootDistance * 0.25))
    );
    const Point returnControl2 = subtract(
        target,
        multiply(tangent, std::max(1.0, distance * 0.08))
    );

    constexpr double firstSegmentEnd = 0.88;
    const double tremorAmplitude = std::clamp(options.tremor_pixels, 0.0, 3.0);

    for (std::size_t index = 0; index < options.sample_count; ++index) {
        const double rawT = static_cast<double>(index) /
            static_cast<double>(options.sample_count - 1);
        const double easedT = sCurve(rawT);

        Point point{};
        if (easedT <= firstSegmentEnd) {
            const double localT = easedT / firstSegmentEnd;
            point = cubicBezier(start, firstControl, secondControl, overshoot, localT);
        } else {
            const double localT = (easedT - firstSegmentEnd) / (1.0 - firstSegmentEnd);
            point = cubicBezier(overshoot, returnControl1, returnControl2, target, localT);
        }

        const double noise = perlin1D(easedT * 7.0, options.seed ^ 0x9E3779B9u);
        const double gaussian = tremorDistribution(random);
        const double offset = (noise * 0.75 + gaussian) * tremorAmplitude;
        result.push_back(add(point, multiply(normal, offset)));
    }

    result.front() = start;
    result.back() = target;
    return result;
}

} // namespace deskawake
