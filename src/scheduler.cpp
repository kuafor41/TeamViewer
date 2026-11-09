#include "deskawake/scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace deskawake {

Scheduler::Scheduler(SchedulerSettings settings, std::uint64_t seed)
    : settings_(settings), random_(seed) {
    if (settings_.minimum_seconds < 1 ||
        settings_.maximum_seconds < settings_.minimum_seconds ||
        settings_.maximum_seconds > 89) {
        throw std::invalid_argument("scheduler bounds must satisfy 1 <= minimum <= maximum <= 89");
    }
    if (!std::isfinite(settings_.weibull_shape) || settings_.weibull_shape <= 0.0 ||
        !std::isfinite(settings_.weibull_scale) || settings_.weibull_scale <= 0.0) {
        throw std::invalid_argument("Weibull shape and scale must be positive and finite");
    }
    if (!std::isfinite(settings_.micro_probability) ||
        settings_.micro_probability < 0.0 || settings_.micro_probability > 1.0) {
        throw std::invalid_argument("micro_probability must be between 0 and 1");
    }
}

std::uint32_t Scheduler::nextDelaySeconds() {
    const double minimum = static_cast<double>(settings_.minimum_seconds);
    const double maximum = static_cast<double>(settings_.maximum_seconds);
    const double shape = settings_.weibull_shape;
    const double scale = settings_.weibull_scale;

    const auto cdf = [shape, scale](double value) {
        return 1.0 - std::exp(-std::pow(value / scale, shape));
    };

    const double lowerCdf = cdf(minimum);
    const double upperCdf = cdf(maximum);
    std::uniform_real_distribution<double> distribution(lowerCdf, upperCdf);
    const double sampledCdf = distribution(random_);
    const double safeSurvival = std::max(1e-12, 1.0 - sampledCdf);
    const double sampled = scale * std::pow(-std::log(safeSurvival), 1.0 / shape);

    const auto rounded = static_cast<std::uint32_t>(std::lround(sampled));
    return std::clamp(rounded, settings_.minimum_seconds, settings_.maximum_seconds);
}

ActionKind Scheduler::nextAction() {
    std::bernoulli_distribution chooseMicro(settings_.micro_probability);
    return chooseMicro(random_) ? ActionKind::Micro : ActionKind::HumanTrajectory;
}

} // namespace deskawake
