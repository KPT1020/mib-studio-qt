# ThreadRegistry

> Name → OS-thread-id map for the pipeline's long-running threads, so
> [[PipelineTrendSampler]] can attribute CPU time and nonvoluntary context
> switches *per pipeline stage* instead of per process.

**Source:** `src/backend/diagnostics/ThreadRegistry.cpp`,
`include/backend/diagnostics/ThreadRegistry.h` (part of `mib_processing`,
Qt-free)
**Related:** [[PipelineTrendSampler]], [[../architecture/Threading-Model]]

## Registered stages

`registerCurrentThread("<name>")` is called once at loop entry by:

| Name | Where |
| --- | --- |
| `capture` | `CaptureService::run` |
| `realtime` | `ProcessingService::realtimeLoop` |
| `batch_worker` | `ProcessingService::batchWorkerLoop` (one per worker; sampler sums) |
| `trigger` | `TriggerService::triggerLoop` |
| `hdf_writer` | `HdfWriteQueue::run` |

## Semantics

- Registration is one mutex acquisition per thread start; re-registration of
  the same tid refreshes the name.
- Entries are never removed: dead tids simply stop moving and the sampler
  skips them (`cpuSeconds < 0`); pipeline threads are recreated under the
  same names.
- Per-thread sampling itself lives in `PipelineTrendSampler`
  (`/proc/self/task/<tid>/stat|status` on Linux, `GetThreadTimes` on
  Windows; context switches are Linux-only).
