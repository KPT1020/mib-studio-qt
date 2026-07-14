# Native processing-core selection

**Date:** 2026-07-13

**Issues:** [A7 #242](https://github.com/KPT1020/mib-studio-qt/issues/242),
[A8 #239](https://github.com/KPT1020/mib-studio-qt/issues/239),
[A10 #241](https://github.com/KPT1020/mib-studio-qt/issues/241),
[A11 #243](https://github.com/KPT1020/mib-studio-qt/issues/243),
[A12 #240](https://github.com/KPT1020/mib-studio-qt/issues/240)

**Plan:** `docs/exec-plans/active/2026-07-13-hot-swappable-processing-cores.md`

## Implemented slice

- Added a C-compatible engine ABI, bundled adapter, independently built native
  module, strict loader, pooled single-owner contexts, and process-lifetime
  module residency.
- Added content-addressed cache preparation with digest validation, atomic
  staging, concurrent lock protection, and stale-lock recovery.
- Added a stable/beta Settings selector that validates the history index and
  the canonical `latest.json` active pointer independently, then validates the
  selected immutable manifest → downloaded bytes → Authenticode signer →
  ABI/self-test before a between-operation activation. A partially published
  `index.active_version` cannot lead `latest.json`. Persisted explicit
  selections are revalidated on startup; administrator hard pins fail closed.
- Routed live/offline/playback mask generation and raw/buffer empty detection
  through the selected kernel. Operation leases keep one identity selected
  through realtime, batch, recording, and buffer-export lifecycles. Added exact
  core identity to both experiment and raw-recording HDF5 provenance.
- Added C ABI, dynamic parity, loader trust, cache concurrency/recovery,
  catalog hardening, activation lifecycle, and HDF5 round-trip tests. The
  pure-C ABI compile guard requires C11 explicitly so MSVC enables the
  `_Static_assert` checks instead of using its legacy default C mode.
- Kept the local active-core identity independent of registry availability:
  the dialog renders it before the first fetch, while preserving any registry
  failure in the separate status label. An offscreen Qt regression covers the
  original blank-label failure without network access.
- Closed the settings-write ordering defect found by desktop E2E: the desktop
  now has a stable organization/application identity, migrates every legacy
  `Unknown Organization` preference once without overwriting current values,
  and commits an exact core selection under the backend activation lock before
  swapping kernels. Deterministic `QSettings` and injected pre-commit failures
  preserve both the prior persisted selection and prior usable kernel.
- Added the production signer gate to every maintained desktop publisher.
  Stable/beta Actions builds require a normalized 64-hex repository SPKI before
  CMake, the manual workflow checks before its version mutation, and the local
  publisher reads the same GitHub variable before every non-skipped build and
  before bumping. Native release CI derives DER-SPKI SHA-256 from the signed
  DLL and rejects a certificate/pin mismatch before upload; development and
  fork builds remain default-off.
- Hardened those desktop publishers against release/source/artifact mismatch.
  Tag dispatch validates and checks out the exact requested tag; manual stable
  CI changes no refs until build, CTest, installer, exact-artifact, and upload
  gates pass, then pushes main/tag atomically. All paths clear stale installer
  outputs and hash/publish only exact numeric-version Setup/Update files. A
  shared resolver bumps beyond reachable stable/beta tags even when the CMake
  fallback is stale; paired CMake overrides plus identity-file readback prove
  the binary's numeric/full version matches the release. All entrypoints build
  the full target set and run CTest before external publication. Local pushes
  require a clean tree (`main` for stable), are atomic, and GitHub/R2 failures
  are fatal. Beta R2 object keys retain the
  full prerelease identity and same-line SHA betas sort by publication time.
  Local dry-run calculates the prospective version, installer failures are
  fatal, and existing release tags are immutable.
- Closed the local A8/A10/A11 release-quality gaps: repaired manylinux wheels
  import in a clean production base; Windows uses independently configured
  good/truncated/incompatible/malformed/throwing modules, export/import audits,
  an ephemeral Authenticode matrix, flat release assets, and a separate
  Production signing job. Activation rejects leases before reset, clears all
  stale state after a committed swap, and passes concurrent A→B→A stress.
  Profiles carry optional processing-contract compatibility, downgrades require
  explicit confirmation, and HDF regeneration warns on recorded/active drift.
- Added a Production promotion/rollback action, stable/beta tag-derived
  channels, and explicit app compatibility bounds. Published and publicly
  verified stable LUT revision `2026.07.14-1`, removing the registry's missing
  cross-link prerequisite.

## Deliberate residual scope

The ABI v1 owns mask generation and empty-frame classification, not the full
processing pipeline. Host code still computes contours, metrics, tracking,
target groups, callbacks, and orchestration from the selected mask. A core
that changes those semantics is not fully replaceable; A7/#242 remains open.

A8/#239 now has independent-source fixtures, import/export audit, a secret-free
signature matrix, and isolated production signing wiring; a real signed runner
execution still belongs to A12. A10/#241 now has A→B→A/reset and concurrent
stress coverage; the remaining live proof is the Windows production path.
A11/#243 now includes profile-contract coupling, downgrade confirmation, and
recorded-vs-active regeneration warnings; live signed Windows selector and
restart rehearsal remain.

Production Authenticode signing and Windows hardware proof need the real
certificate/infrastructure and remain A12 gates. R2 access is present, the
stable LUT is live, and promotion/rollback is wired but cannot run until a
first immutable core exists. The selector's production trust root is the signer public key's DER
SPKI SHA-256 compiled into the application; production environment variables
cannot replace it. The release gate is wired, but the real certificate secrets
and repository pin still need provisioning and remain an A12 release
configuration gate. The Linux sandbox also has no PowerShell/Windows packaging
toolchain, so the static release regressions and YAML checks still require a
real stable+beta Windows rehearsal. No live release was loaded in the sandbox.

**Related:** [[../frontend/ProcessingCoreDialog]] ·
[[../services/ProcessingService]] · [[../services/Hdf5Service]] ·
[[2026-07-13-processing-core-registry]]
