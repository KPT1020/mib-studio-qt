Title: Capture stats show 0 fps and 0 MB/s at test end

Context
- Observed log after a 2s run shows: "Frames processed: 7500 | Last fps: 0 | MB/s: 0".
- Periodic capture log sometimes shows: "Capture stats: 1920x1080, 0 MB/s, 0 fps".
- Config script `egrabberConfig.js` sets 512x96 with trigger on LinkTrigger0; CPU job logs during run show 512x96 frames, so streaming occurs when configured.

Relevant code
- Stats update happens once per second inside the capture loop using Euresys StreamModule counters:

```93:99:src/backend/services/CaptureService.cpp
                uint64_t fr = grabber.getInteger<StreamModule>("StatisticsFrameRate");
                uint64_t dr = grabber.getInteger<StreamModule>("StatisticsDataRate");
                stats_.lastFrameRate.store(fr, std::memory_order_relaxed);
                stats_.lastDataRateMBps.store(dr, std::memory_order_relaxed);
                SPDLOG_INFO("Capture stats: {}x{}, {} MB/s, {} fps", width, height, dr, fr);
```

Likely causes
1) Final print happens after `cap.stop()`. If the 1s periodic update did not run yet (e.g., first buffer took >1s due to BufferPartCount=100 or 500ms warm-up), `lastFrameRate`/`lastDataRateMBps` remain 0.
2) When the device isn’t actually streaming (no triggers with `TriggerMode=On` and `TriggerSource=LinkTrigger0`), counters stay 0. Prior logs showing 1920x1080 indicate runs without the config script applied.

What to try
- Extend test duration (e.g., 10s) or lower `bufferPartCount` to allow the 1s stats update to fire before stop.
- Move a stats read just before stopping (while still running) or update stats every loop iteration.
- Verify streaming: temporarily set `TriggerMode=Off` to free-run to confirm counters > 0, then restore trigger mode.
- If needed, compute fps/MB/s from part timestamps and sizes (BUFFER_INFO_CUSTOM_PART_TIMESTAMPS and imageSize) as a fallback independent of StreamModule counters.

References
- Euresys sample matches this pattern and reads the same counters each second:

```39:46:egrabber-win-sample-programs-25.02.0.41/egrabber-sample-programs/cpp/egrabber-snippets/samples/310-high-frame-rate.cpp
        if (t >= tShowStats) {
            uint64_t fr = grabber.getInteger<StreamModule>("StatisticsFrameRate");
            uint64_t dr = grabber.getInteger<StreamModule>("StatisticsDataRate");
            Tools::log(Tools::toString(width) + "x" + Tools::toString(height) + " : " +
                       Tools::toString(dr) + " MB/s, " +
                       Tools::toString(fr) + " fps");
            tShowStats += static_cast<uint64_t>(1e6);
        }
```

Status
- Root cause and next steps documented.

