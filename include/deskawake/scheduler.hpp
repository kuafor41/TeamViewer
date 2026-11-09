#pragma once

#include <cstdint>
#include <random>

namespace deskawake {

enum class ActionKind {
    Micro,
    HumanTrajectory
};

struct SchedulerSettings {
    std::uint32_t minimum_seconds{10};
    std::uint32_t maximum_seconds{89};
    double weibull_shape{2.0};
    double weibull_scale{48.0};
    double micro_probability{0.70};
};

class Scheduler {
public:
    explicit Scheduler(
        SchedulerSettings settings = {},
        std::uint64_t seed = std::random_device{}()
    );

    [[nodiscard]] std::uint32_t nextDelaySeconds();
    [[nodiscard]] ActionKind nextAction();

private:
    SchedulerSettings settings_;
    std::mt19937_64 random_;
};

} // namespace deskawake
