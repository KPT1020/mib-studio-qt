// autofocus_math_test
//
// Pins down the autofocus control decision (extracted to
// backend::services::autofocus). A wrong direction, step size, or missing clamp
// silently drives the nanopositioner probe past its safe range, so this covers
// the coarse/fine/hold branches, the focusDirection inversion, and clamping at
// both limits — including the case where the starting voltage is already out of
// range.

#include "backend/services/AutofocusMath.h"

#include "support/assert.h"

#include <cmath>
#include <string>

namespace af = backend::services::autofocus;

namespace {
bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }

// Reference params: setpoint 20, range 0.5, coarse 1.0, fine 0.2, [0, 100],
// focusDirection true (more voltage -> higher ring ratio).
af::FocusParams base()
{
    return af::FocusParams{/*setpoint*/ 20.0, /*range*/ 0.5, /*voltageStep*/ 1.0,
                           /*fineVoltageStep*/ 0.2, /*minVoltage*/ 0.0,
                           /*maxVoltage*/ 100.0, /*focusDirection*/ true};
}
} // namespace

int main()
{
    // 1) clampVoltage basics, including misconfigured limits.
    {
        MIB_EXPECT(near(af::clampVoltage(50.0, 0.0, 100.0), 50.0), "in-range unchanged");
        MIB_EXPECT(near(af::clampVoltage(-5.0, 0.0, 100.0), 0.0), "below clamps to min");
        MIB_EXPECT(near(af::clampVoltage(150.0, 0.0, 100.0), 100.0), "above clamps to max");
        // hi < lo: don't fabricate a bound, leave the value alone.
        MIB_EXPECT(near(af::clampVoltage(42.0, 100.0, 0.0), 42.0), "inverted limits pass through");
    }

    // 2) Within half the band (|deviation| <= range/2) -> hold (no change).
    {
        const af::FocusParams p = base();
        // median == setpoint exactly.
        MIB_EXPECT(near(af::computeFocusVoltage(20.0, 50.0, p), 50.0), "on setpoint holds");
        // deviation 0.2 < range/2 (0.25) -> hold.
        MIB_EXPECT(near(af::computeFocusVoltage(20.2, 50.0, p), 50.0), "inside half-band holds");
        MIB_EXPECT(near(af::computeFocusVoltage(19.8, 50.0, p), 50.0), "inside half-band holds (below)");
    }

    // 3) Fine step: inside the acceptable range but beyond half the band.
    {
        const af::FocusParams p = base();
        // median below setpoint, direction true -> need to increase by fine step.
        // deviation -0.4: |0.4| <= 0.5 (in range) and > 0.25 (beyond half).
        MIB_EXPECT(near(af::computeFocusVoltage(19.6, 50.0, p), 50.2), "fine step up below setpoint");
        // median above setpoint -> decrease by fine step.
        MIB_EXPECT(near(af::computeFocusVoltage(20.4, 50.0, p), 49.8), "fine step down above setpoint");
    }

    // 4) Coarse step: outside the acceptable range (|deviation| > range).
    {
        const af::FocusParams p = base();
        MIB_EXPECT(near(af::computeFocusVoltage(15.0, 50.0, p), 51.0), "coarse step up far below");
        MIB_EXPECT(near(af::computeFocusVoltage(25.0, 50.0, p), 49.0), "coarse step down far above");
    }

    // 5) focusDirection inversion flips the sign of every correction.
    {
        af::FocusParams p = base();
        p.focusDirection = false; // more voltage -> lower ring ratio
        // median far below setpoint now needs LESS voltage.
        MIB_EXPECT(near(af::computeFocusVoltage(15.0, 50.0, p), 49.0), "inverted: coarse down far below");
        MIB_EXPECT(near(af::computeFocusVoltage(25.0, 50.0, p), 51.0), "inverted: coarse up far above");
    }

    // 6) Clamping at the limits: a step can never exceed [min, max].
    {
        const af::FocusParams p = base();
        // Near max, coarse step up would exceed 100 -> clamps.
        MIB_EXPECT(near(af::computeFocusVoltage(15.0, 99.5, p), 100.0), "coarse up clamps at max");
        // Near min, coarse step down would go below 0 -> clamps.
        MIB_EXPECT(near(af::computeFocusVoltage(25.0, 0.5, p), 0.0), "coarse down clamps at min");
    }

    // 7) Defensive clamp: a starting voltage already outside the range is pulled
    //    back even when the ring ratio is on-setpoint (the old per-branch min/max
    //    left this uncorrected).
    {
        const af::FocusParams p = base();
        MIB_EXPECT(near(af::computeFocusVoltage(20.0, 150.0, p), 100.0), "out-of-range high pulled to max");
        MIB_EXPECT(near(af::computeFocusVoltage(20.0, -10.0, p), 0.0), "out-of-range low pulled to min");
    }

    if (mib::test::exitCode() == 0) {
        std::printf("Autofocus control math (step/clamp/direction) verified\n");
    }
    return mib::test::exitCode();
}
