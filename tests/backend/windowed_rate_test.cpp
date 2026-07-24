// windowed_rate_test
//
// Guards camera::common::WindowedRate, the host-side per-poll rate estimator
// MindVisionCamera::pollStats uses (the MVSDK reports no device-side rates).
// The previous cumulative since-start average went stale after rate changes
// and diluted toward zero across idle stretches in trigger mode; these cases
// pin the windowed semantics with synthetic timestamps (no sleeping).

#include "backend/camera/common/WindowedRate.h"

#include "support/assert.h"

#include <chrono>
#include <cstdio>

using camera::common::WindowedRate;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

int main()
{
    const auto t0 = steady_clock::now();

    // 1) First sample establishes the baseline and reports 0.
    {
        WindowedRate r;
        MIB_EXPECT(r.sample(100, t0) == 0.0, "first sample has no window, reports 0");
    }

    // 2) Steady stream: rate equals frames-in-window / window.
    {
        WindowedRate r;
        r.sample(0, t0);
        const double rate = r.sample(500, t0 + milliseconds(1000));
        MIB_EXPECT(rate > 499.0 && rate < 501.0, "500 frames over 1s reads ~500 fps");
    }

    // 3) Rate change is reflected immediately in the next window, not diluted
    //    by history (the cumulative average would still read ~275 here).
    {
        WindowedRate r;
        r.sample(0, t0);
        r.sample(500, t0 + milliseconds(1000));
        const double rate = r.sample(550, t0 + milliseconds(2000));
        MIB_EXPECT(rate > 49.0 && rate < 51.0, "drop to 50 fps reads ~50 next window");
    }

    // 4) Idle window (trigger mode): reads 0, not a decaying average.
    {
        WindowedRate r;
        r.sample(0, t0);
        r.sample(500, t0 + milliseconds(1000));
        const double rate = r.sample(500, t0 + milliseconds(6000));
        MIB_EXPECT(rate == 0.0, "no frames in window reads 0");
    }

    // 5) Sub-millisecond window: returns the last rate instead of spiking.
    {
        WindowedRate r;
        r.sample(0, t0);
        r.sample(100, t0 + milliseconds(1000));
        const double before = r.lastRate();
        const double rate = r.sample(101, t0 + milliseconds(1000));
        MIB_EXPECT(rate == before, "zero-length window keeps last rate");
    }

    // 6) Counter reset (camera restart): resynchronizes without underflow.
    {
        WindowedRate r;
        r.sample(1000, t0);
        const double rate = r.sample(10, t0 + milliseconds(1000));
        MIB_EXPECT(rate == 0.0, "count regression reads 0, not an underflow spike");
        const double next = r.sample(110, t0 + milliseconds(2000));
        MIB_EXPECT(next > 99.0 && next < 101.0, "resynchronized after regression");
    }

    // 7) reset() clears history: next sample is a fresh baseline.
    {
        WindowedRate r;
        r.sample(0, t0);
        r.sample(500, t0 + milliseconds(1000));
        r.reset();
        MIB_EXPECT(r.sample(9999, t0 + milliseconds(2000)) == 0.0,
                   "post-reset sample is a baseline, not a delta");
    }

    if (mib::test::exitCode() == 0) {
        std::printf("WindowedRate semantics verified\n");
    }
    return mib::test::exitCode();
}
