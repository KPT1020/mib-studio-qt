# Capture stats show 0 fps / 0 MB/s

- Context: Console test `capture_processing_test` executed for ~2s and processed frames successfully (CPU jobs logged), but StreamModule stats stayed at zero.
- Evidence (log excerpt):
  - [1019] CPU job: 1920x1080, ts=0 ns, cksum=...
  - [1020] Capture stats: 1920x1080, 0 MB/s, 0 fps
  - [1021] CaptureService stopped
- Current implementation:
  - Polls `StreamModule` integers: `StatisticsFrameRate`, `StatisticsDataRate` (once per ~1s) in `CaptureService`.
  - Timestamp read via `gc::BUFFER_INFO_TIMESTAMP_NS` returned 0 in logs.

Assumptions/Policy:
- Follow Euresys SDK and provided samples strictly; avoid custom fallback metrics. Fix root causes per SDK best practice.

Hypotheses:
- Stats may require specific driver/camera state (no camera or producer stats disabled).
- Reading timestamp and stats order may matter relative to buffer lifecycle.
- Property/module name or availability may differ per board; confirm against samples `310/311`.

Next steps (SDK-aligned):
- Verify camera/GenTL producer actually streaming and stats enabled; mirror `310/311` exactly for polling cadence and buffer handling.
- Move/confirm the timing of timestamp/stat queries per sample order (e.g., after `ScopedBuffer` pop if required by driver semantics).
- If still zero, consult Euresys docs for board-specific stat nodes and enablement flags; adjust node names accordingly.
