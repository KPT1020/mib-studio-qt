// hw_nanopositioner_test  (LABEL: hardware) — runs only against a real stage.
//
// Set MIB_TEST_NANOPOSITIONER_PORT to the COM number of the CoreMOR
// nanopositioner. Optional: MIB_TEST_NANOPOSITIONER_BAUD (default 115200),
// MIB_TEST_NANOPOSITIONER_ADDR (default 1). Skips when the port env is absent.

#include "backend/services/AutofocusService.h"

#include "support/assert.h"
#include "support/hardware.h"

#include <cstdio>
#include <cstdlib>

using backend::services::AutofocusService;

int main()
{
    const int port = std::atoi(mib::test::requireDeviceEnv("MIB_TEST_NANOPOSITIONER_PORT"));
    const int baud = mib::test::envInt("MIB_TEST_NANOPOSITIONER_BAUD", 115200);
    const int addr = mib::test::envInt("MIB_TEST_NANOPOSITIONER_ADDR", 1);

    // Probe: opens the port, reads a voltage, validates 0-250 V. Must succeed
    // before connecting.
    MIB_EXPECT(AutofocusService::probeComPort(port, baud, static_cast<unsigned char>(addr)),
               "probeComPort returns a plausible voltage");

    AutofocusService af;
    MIB_REQUIRE(af.connect(port, baud, static_cast<unsigned char>(addr)),
                "connect to nanopositioner");
    MIB_EXPECT(af.isConnected(), "nanopositioner reports connected");
    af.disconnect();
    MIB_EXPECT(!af.isConnected(), "nanopositioner disconnects cleanly");

    if (mib::test::exitCode() == 0) std::printf("nanopositioner hardware OK\n");
    return mib::test::exitCode();
}
