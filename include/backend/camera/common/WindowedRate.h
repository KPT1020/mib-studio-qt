// Windowed (per-poll delta) rate estimator for cameras whose SDK does not
// report device-side statistics. A cumulative since-start average dilutes to
// meaninglessness in trigger mode (long idle stretches) and hides rate changes;
// this computes the rate over the interval since the previous sample instead.
// Pure and SDK-free so it is unit-testable on any platform.
#pragma once

#include <chrono>
#include <cstdint>

namespace camera::common
{

    class WindowedRate
    {
    public:
        // Record a new cumulative frame count at the given time and return the
        // rate (per second) over the window since the previous sample. The
        // first sample and any sample with a non-positive or sub-millisecond
        // window return the last known rate (0 initially) rather than a spike
        // from dividing by a tiny interval.
        double sample(std::uint64_t cumulativeCount, std::chrono::steady_clock::time_point now)
        {
            if (!hasSample_)
            {
                hasSample_ = true;
                lastCount_ = cumulativeCount;
                lastTime_ = now;
                return lastRate_;
            }

            const double elapsed = std::chrono::duration<double>(now - lastTime_).count();
            if (elapsed < 0.001)
            {
                return lastRate_;
            }

            // A restart can lower the cumulative count; resynchronize.
            const std::uint64_t delta =
                cumulativeCount >= lastCount_ ? cumulativeCount - lastCount_ : 0;
            lastRate_ = static_cast<double>(delta) / elapsed;
            lastCount_ = cumulativeCount;
            lastTime_ = now;
            return lastRate_;
        }

        void reset()
        {
            hasSample_ = false;
            lastCount_ = 0;
            lastRate_ = 0.0;
        }

        double lastRate() const { return lastRate_; }

    private:
        bool hasSample_{false};
        std::uint64_t lastCount_{0};
        std::chrono::steady_clock::time_point lastTime_{};
        double lastRate_{0.0};
    };

} // namespace camera::common
