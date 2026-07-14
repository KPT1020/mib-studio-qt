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
  catalog hardening, activation lifecycle, and HDF5 round-trip tests.
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

## Deliberate residual scope

The ABI v1 owns mask generation and empty-frame classification, not the full
processing pipeline. Host code still computes contours, metrics, tracking,
target groups, callbacks, and orchestration from the selected mask. A core
that changes those semantics is not fully replaceable; A7/#242 remains open.

A8/#239 still needs an independently built/precompiled Windows ABI fixture and
import/signature audit beyond the in-tree dynamic-module parity fixture.
A10/#241 still needs A→B→A/reset and TSan/concurrency stress coverage; settings
sync failure rollback is now covered before activation, so there is no
post-activation persistence failure state.
A11/#243 still needs profile-contract coupling, explicit scientific-review
warnings, richer cached/ready state, downgrade confirmation, and final selector
polish. These issues are not closed by this foundation.

Production Authenticode signing, R2 publication, release promotion/rollback,
and Windows hardware proof need live secrets/infrastructure and remain A12
gates. The selector's production trust root is the signer public key's DER
SPKI SHA-256 compiled into the application; production environment variables
cannot replace it. The release gate is wired, but the real certificate secrets
and repository pin still need provisioning and remain an A12 release
configuration gate. No live release was loaded in the sandbox.

**Related:** [[../frontend/ProcessingCoreDialog]] ·
[[../services/ProcessingService]] · [[../services/Hdf5Service]] ·
[[2026-07-13-processing-core-registry]]
