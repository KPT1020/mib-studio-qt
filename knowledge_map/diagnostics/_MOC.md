# Diagnostics — MOC

> Cross-cutting observability and crash-reporting infrastructure that all
> services participate in.

## Notes

- [[CrashStateMirror]] — lock-free state snapshot store read by the crash
  handler to produce per-crash `.json` sidecars.
- [[PipelineTimingRecorder]] — lock-free per-frame latency recorder
  (acquisition → algorithm → trigger stamps + skip accounting) behind the
  pipeline-delay diagnosis workflow.
- [[PipelineTrendSampler]] — 1 Hz time-series consumer of the recorder
  (per-stage percentiles, queue depths, backlog, per-thread CPU, heap,
  allocation churn, RSS into `pipeline_trend.csv`) for
  latency-growth-over-minutes investigations.
- [[ThreadRegistry]] — name → OS-thread-id map of the pipeline's
  long-running threads, enabling per-stage CPU / context-switch
  attribution in the trend sampler.
- [[MatAllocStats]] — delegating cv::MatAllocator counting every Mat
  allocation (count + bytes): the direct per-frame heap-churn measurement.

## Related services / conventions

- [[../services/CrashReporter]] — installs SEH/signal/Qt/terminate
  handlers, writes minidumps, optionally forwards to Sentry.
- [[../conventions/Logging]] — spdlog routing and the crash-reporting
  section that points here.

**Up**: [[../README|Vault home]]
