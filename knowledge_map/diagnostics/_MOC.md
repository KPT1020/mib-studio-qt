# Diagnostics — MOC

> Cross-cutting observability and crash-reporting infrastructure that all
> services participate in.

## Notes

- [[CrashStateMirror]] — lock-free state snapshot store read by the crash
  handler to produce per-crash `.json` sidecars.

## Related services / conventions

- [[../services/CrashReporter]] — installs SEH/signal/Qt/terminate
  handlers, writes minidumps, optionally forwards to Sentry.
- [[../conventions/Logging]] — spdlog routing and the crash-reporting
  section that points here.

**Up**: [[../README|Vault home]]
