# 2026-05-23 — Sentry monorepo setup

## Context

`main` gained process-level crash capture on 2026-05-22, but release metadata
and operator workflow were still single-app oriented. For shared-repository
deployments we need stable release names that stay unique across projects and a
repeatable `sentry-cli` flow.

## Changes

1. **Runtime config upgrades** (`src/frontend/core/main.cpp`)
   - Added env fallback order:
     - DSN: `MIB_SENTRY_DSN` → `SENTRY_DSN`
     - environment: `MIB_CRASH_ENV` → `SENTRY_ENVIRONMENT`
     - release: `MIB_SENTRY_RELEASE` → `SENTRY_RELEASE` → auto
   - Added `MIB_SENTRY_COMPONENT` (`mib-studio-qt/desktop` default).
   - Auto release format now: `<component>@<MIB_STUDIO_QT_VERSION>+<short_sha>`
     when explicit release is not provided.
   - Namespaced Sentry/Crashpad database path by component to avoid collisions
     when multiple apps from one repo share the same crash root.

2. **Release automation script** (`scripts/sentry-release-monorepo.sh`)
   - Creates release for one or many projects (`SENTRY_PROJECT` or
     `SENTRY_PROJECTS`).
   - Associates commits (`--auto --ignore-missing`, fallback `--local`).
   - Optionally uploads debug files passed as script args.
   - Finalizes release + records deploy event.

3. **Runbooks**
   - Added `docs/howto/sentry-monorepo.md`.
   - Linked new runbook from `docs/README.md`.
   - Updated knowledge notes:
     - `knowledge_map/services/CrashReporter.md`
     - `knowledge_map/build-and-run/Run-Modes.md`
     - `knowledge_map/current-state/Recent-Work.md`

## Validation

- Verified script shell syntax with `bash -n scripts/sentry-release-monorepo.sh`.
- Verified knowledge map wikilinks via `rg '\[\[' knowledge_map/`.

## Follow-ups

- Wire `scripts/sentry-release-monorepo.sh` into CI release workflow once a
  production Sentry org/project mapping is finalized.
