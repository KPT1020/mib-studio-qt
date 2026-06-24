// Pure autofocus control math, extracted from AutofocusService so the focus
// decision and voltage clamping can be unit tested without the CoreMOR device.
#pragma once

#include <algorithm>
#include <cmath>

namespace backend::services::autofocus {

struct FocusParams {
    double setpoint{0.0};
    double range{0.0};
    double voltageStep{0.0};
    double fineVoltageStep{0.0};
    double minVoltage{0.0};
    double maxVoltage{0.0};
    bool focusDirection{true};
};

// Clamp to [lo, hi]; if the limits are misconfigured (hi < lo) leave the value
// untouched rather than fabricate a bound.
inline double clampVoltage(double v, double lo, double hi)
{
    if (hi < lo) return v;
    return std::clamp(v, lo, hi);
}

// Next stage voltage given the current ring-ratio median vs the setpoint.
// Outside the acceptable range it takes a coarse step toward focus; inside the
// range but beyond half the band it takes a fine step; within half the band it
// holds. The result is always clamped to [minVoltage, maxVoltage] (so an
// out-of-range starting voltage is pulled back even when no step is needed).
inline double computeFocusVoltage(double medianRingRatio, double currentVoltage,
                                  const FocusParams& p)
{
    double v = currentVoltage;
    const double deviation = medianRingRatio - p.setpoint;
    const bool inAcceptableRange = std::abs(deviation) <= p.range;
    const bool needIncrease =
        (deviation < 0 && p.focusDirection) || (deviation > 0 && !p.focusDirection);

    if (!inAcceptableRange) {
        v = needIncrease ? std::min(v + p.voltageStep, p.maxVoltage)
                         : std::max(v - p.voltageStep, p.minVoltage);
    } else if (std::abs(deviation) > p.range / 2.0) {
        v = needIncrease ? std::min(v + p.fineVoltageStep, p.maxVoltage)
                         : std::max(v - p.fineVoltageStep, p.minVoltage);
    }
    return clampVoltage(v, p.minVoltage, p.maxVoltage);
}

} // namespace backend::services::autofocus
