#pragma once
#include <cstddef>
#include <cstdint>
#include <random>
namespace deskawake {
enum class ActionKind { FastTrajectory, HumanTrajectory };
[[nodiscard]] constexpr std::size_t trajectorySampleCount(ActionKind action) noexcept {
    return action == ActionKind::FastTrajectory ? 8U : 28U;
}
struct SchedulerSettings {
    std::uint32_t minimum_seconds{10};
    std::uint32_t maximum_seconds{89};
    double poisson_lambda{42.0};
    double fast_trajectory_probability{0.70};
};
class Scheduler {
public:
    explicit Scheduler(SchedulerSettings settings = {},
                       std::uint64_t seed = std::random_device{}());
    [[nodiscard]] std::uint32_t nextDelaySeconds();
    [[nodiscard]] ActionKind nextAction();
private:
    SchedulerSettings settings_;
    std::mt19937_64 random_;
};
} // namespace deskawake
