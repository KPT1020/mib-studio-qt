# Logging

> All runtime logging goes through spdlog. Configured once by
> `backend::services::Logger::init(path)` during `AppBackend::initialize`.

**Source:** `src/backend/services/Logger.cpp`,
`include/backend/services/Logger.h`

## How to log

```cpp
#include <spdlog/spdlog.h>

SPDLOG_INFO("capture started: buffers={}", config.numBuffers);
SPDLOG_WARN("YOLO model not loaded - segmentation features will not be available");
SPDLOG_ERROR("failed to open {}: {}", path, err);
```

Prefer the `SPDLOG_<LEVEL>` macros over `spdlog::info(...)` because they
capture source file/line info.

## Log file location

`AppBackend::initialize` picks a user-writable location:

- Default: `<dataDir>/logs/app.log`.
- On Windows, if `dataDir` is under `Program Files`, fall back to
  `%LOCALAPPDATA%/MIB_Studio_Qt/logs/app.log`.

## Conventions

- **Never** use `std::cout` / `std::cerr` / `printf` / `qDebug`. Routing
  must be consistent for rotation and troubleshooting. See
  `knowledge_map/task/review_logging_improvements.md` for prior work.
- Every service's start and stop should log at INFO with a one-line
  summary (what it's doing, not what it "will do").
- Error logs should include enough context to triage without opening a
  debugger (path, errno-equivalent, device ID).

## Diagnostic logging

See `knowledge_map/task/diagnostic-logging.md` for the extended
diagnostic pattern used during camera lifecycle issues.
