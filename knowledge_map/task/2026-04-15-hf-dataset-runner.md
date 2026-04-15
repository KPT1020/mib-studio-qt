Title: HuggingFace dataset offline runner + CNN training-data export

Scope
- Feed a standard HuggingFace image dataset (e.g. `gavinlouuu/512x96stream`)
  through the production C++ `ProcessingService` to (1) verify the classical
  pipeline on a controlled reference set and (2) collect labelled outputs for
  a future CNN replacement.

Implementation
- `scripts/hf_dataset_download.py` — Python downloader that fetches a HF
  dataset split and writes numbered grayscale PNGs into a folder consumed
  as-is by [[../camera/MockCamera]] (via `MIB_CAMERA_MODE=mock` /
  `MIB_MOCK_CAMERA_DIR=<folder>`). Writes a `hf_manifest.json` sidecar for
  provenance. Deps added to `scripts/requirements.txt`:
  `datasets`, `huggingface_hub`, `Pillow`.
- `src/tools/hf_pipeline_runner.cpp` — headless C++ CLI (new executable
  target `hf_pipeline_runner`, wired in `CMakeLists.txt` next to
  `capture_processing_test`). Bootstraps `backend::AppBackend`, sets the
  mock-camera env vars before `initialize`, applies a `ProcessingConfig`
  parsed from `config.json` with `nlohmann_json` (mirroring the
  `AppConfigWatcher` key logic — see [[../frontend/System-Utilities]]),
  and runs the exact UI
  experiment lifecycle: `openFile → initializeDatasets → capture.start →
  processing.startRealtime → processing.startExperiment → drain →
  flushBufferedFrames → endExperiment → appendFrames(tail) →
  writeExperimentInfo → writeConfigJson → closeFile`. Defaults
  `invalidFrameSamplingRate = 1` so every rejected frame lands in HDF5 for
  CNN negatives.
- `scripts/hf_cnn_export.py` — post-processing that reads the experiment
  HDF5 (reusing `read_experiment_info` / `read_hdf5_metadata` from
  `export_hdf5.py`) and emits a flat training layout:
  `images/<i>.png`, `masks/<i>.png`, `labels.csv`, `dataset_info.json`,
  optional `background.png`. `labels.csv` includes provenance
  (`source_dataset`, `source_split`, `source_row`) plus a
  `rejection_reason` derived from stored metadata with the same precedence
  as `ProcessingService::filterProcessedImage` — the taxonomy matches the
  `REASON_*` constants in [[../../scripts/reanalyse_hdf5.py]] so downstream
  tools stay consistent.
- `docs/howto/hf-dataset-verification.md` — end-to-end how-to covering
  download, run, export, and troubleshooting.

Notes / gotchas
- The drain loop uses `capture.isRunning() == false AND
  jobsQueued == jobsDone` as the completion predicate; this works because
  `MockCamera` with `loopFiles=false` exits cleanly when it exhausts the
  preloaded frame list. A 30 s no-progress watchdog guards against hangs
  on malformed input.
- `writeExperimentInfo`'s `totalValid`/`totalInvalid` mirror the
  [[../frontend/MainWindow]] `stopExperiment` behavior: only the
  in-memory tail after the final `flushBufferedFrames`; the HDF5 datasets
  themselves are the authoritative count.
- Nothing in `ProcessingService` or `Hdf5Service` had to change —
  everything reuses existing public APIs. This is important for the
  "verification" goal: what runs here is byte-for-byte the production
  algorithm.

Related notes
- [[../camera/MockCamera]], [[../architecture/AppBackend]],
  [[../services/ProcessingService]], [[../services/Hdf5Service]],
  [[../frontend/Controllers]] (`ExperimentController` — lifecycle reference).
