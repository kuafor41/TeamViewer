#include "deskawake/motion.hpp"
#include "deskawake/scheduler.hpp"

#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void testBezierEndpoints() {
    const deskawake::Point p0{2.0, 4.0};
    const deskawake::Point p1{10.0, 3.0};
    const deskawake::Point p2{12.0, 18.0};
    const deskawake::Point p3{30.0, 20.0};
    const auto start = deskawake::cubicBezier(p0, p1, p2, p3, 0.0);
    const auto finish = deskawake::cubicBezier(p0, p1, p2, p3, 1.0);
    require(std::abs(start.x - p0.x) < 1e-12 && std::abs(start.y - p0.y) < 1e-12,
        "Bezier start endpoint mismatch");
    require(std::abs(finish.x - p3.x) < 1e-12 && std::abs(finish.y - p3.y) < 1e-12,
        "Bezier end endpoint mismatch");
}

void testSCurve() {
    require(deskawake::sCurve(-1.0) == 0.0, "S-curve must clamp below zero");
    require(deskawake::sCurve(0.0) == 0.0, "S-curve start must be zero");
    require(deskawake::sCurve(1.0) == 1.0, "S-curve end must be one");
    require(deskawake::sCurve(2.0) == 1.0, "S-curve must clamp above one");
    double previous = 0.0;
    for (int index = 1; index <= 1000; ++index) {
        const double current = deskawake::sCurve(static_cast<double>(index) / 1000.0);
        require(current >= previous, "S-curve must be monotonic");
        previous = current;
    }
}

void testPerlinDeterminism() {
    const double first = deskawake::perlin1D(1.25, 12345);
    const double second = deskawake::perlin1D(1.25, 12345);
    require(first == second, "Perlin noise must be deterministic for a fixed seed");
    require(first >= -1.0 && first <= 1.0, "Perlin noise must remain bounded");
}

void testTrajectory() {
    const deskawake::Point start{20.0, 40.0};
    const deskawake::Point target{420.0, 240.0};
    deskawake::TrajectoryOptions options{};
    options.sample_count = 64;
    options.seed = 42;
    const auto path = deskawake::generateHumanTrajectory(start, target, options);
    require(path.size() == options.sample_count, "trajectory sample count mismatch");
    require(path.front().x == start.x && path.front().y == start.y,
        "trajectory must start exactly at input point");
    require(path.back().x == target.x && path.back().y == target.y,
        "trajectory must end exactly at target");
    for (const auto& point : path) {
        require(std::isfinite(point.x) && std::isfinite(point.y),
            "trajectory contains non-finite coordinates");
    }
    const auto repeated = deskawake::generateHumanTrajectory(start, target, options);
    require(repeated.size() == path.size(), "repeated trajectory size mismatch");
    for (std::size_t index = 0; index < path.size(); ++index) {
        require(path[index].x == repeated[index].x && path[index].y == repeated[index].y,
            "trajectory must be deterministic for a fixed seed");
    }
}

void testSchedulerBoundsAndDistribution() {
    deskawake::Scheduler scheduler({}, 0xC0FFEEu);
    constexpr int sampleCount = 10000;
    int microCount = 0;
    for (int index = 0; index < sampleCount; ++index) {
        const std::uint32_t delay = scheduler.nextDelaySeconds();
        require(delay >= 10 && delay <= 89, "scheduler interval must remain in [10, 89]");
        if (scheduler.nextAction() == deskawake::ActionKind::Micro) ++microCount;
    }
    const double ratio = static_cast<double>(microCount) / sampleCount;
    require(ratio > 0.66 && ratio < 0.74, "router micro-action ratio is outside tolerance");
}

void testInvalidInputs() {
    bool threw = false;
    try {
        deskawake::TrajectoryOptions options{};
        options.sample_count = 1;
        (void)deskawake::generateHumanTrajectory({0.0, 0.0}, {1.0, 1.0}, options);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "trajectory must reject fewer than two samples");

    threw = false;
    try {
        deskawake::SchedulerSettings settings{};
        settings.minimum_seconds = 10;
        settings.maximum_seconds = 90;
        (void)deskawake::Scheduler(settings, 1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "scheduler must reject inverted interval bounds");
}

} // namespace

int main() {
    try {
        testBezierEndpoints();
        testSCurve();
        testPerlinDeterminism();
        testTrajectory();
        testSchedulerBoundsAndDistribution();
        testInvalidInputs();
        std::cout << "All DeskAwake core tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
