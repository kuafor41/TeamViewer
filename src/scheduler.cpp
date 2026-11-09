#include "deskawake/scheduler.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace deskawake {
Scheduler::Scheduler(SchedulerSettings settings, std::uint64_t seed)
    : settings_(settings), random_(seed) {
    if (settings_.minimum_seconds < 10 ||
        settings_.maximum_seconds < settings_.minimum_seconds ||
        settings_.maximum_seconds > 89) {
        throw std::invalid_argument("scheduler bounds must satisfy 10 <= minimum <= maximum <= 89");
    }
    if (!std::isfinite(settings_.poisson_lambda) || settings_.poisson_lambda <= 0.0) {
        throw std::invalid_argument("Poisson lambda must be positive and finite");
    }
    if (!std::isfinite(settings_.micro_probability) ||
        settings_.micro_probability < 0.0 || settings_.micro_probability > 1.0) {
        throw std::invalid_argument("micro_probability must be between 0 and 1");
    }
}
std::uint32_t Scheduler::nextDelaySeconds() {
    // T_interval = min(89, max(10, Poisson(lambda = 42))).
    std::poisson_distribution<std::uint32_t> distribution(settings_.poisson_lambda);
    return std::clamp(distribution(random_), settings_.minimum_seconds, settings_.maximum_seconds);
}
ActionKind Scheduler::nextAction() {
    std::bernoulli_distribution chooseMicro(settings_.micro_probability);
    return chooseMicro(random_) ? ActionKind::Micro : ActionKind::HumanTrajectory;
}
} // namespace deskawake
