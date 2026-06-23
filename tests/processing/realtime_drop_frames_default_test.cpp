// realtime_drop_frames_default_test
//
// Unit test guarding the fix for live-view processed-overlay backlog: realtime
// drop-frames must default to ON so the live overlay tracks the newest frame
// and cannot accumulate a backlog. Also verifies the toggle round-trips.
//
// (Experiments are unaffected because the realtime loop gates this flag behind
// !experimentActive_; that interaction is covered by the e2e test.)

#include "backend/processing/ProcessingService.h"

#include <iostream>

int main()
{
    backend::services::ProcessingService proc;

    if (!proc.getRealtimeDropFrames()) {
        std::cerr << "FAIL: realtime drop-frames should default to ON so the live "
                     "overlay does not accumulate a backlog\n";
        return 1;
    }

    proc.setRealtimeDropFrames(false);
    if (proc.getRealtimeDropFrames()) {
        std::cerr << "FAIL: setRealtimeDropFrames(false) did not take effect\n";
        return 2;
    }

    proc.setRealtimeDropFrames(true);
    if (!proc.getRealtimeDropFrames()) {
        std::cerr << "FAIL: setRealtimeDropFrames(true) did not take effect\n";
        return 3;
    }

    std::cout << "realtime drop-frames defaults ON and toggles correctly\n";
    return 0;
}
