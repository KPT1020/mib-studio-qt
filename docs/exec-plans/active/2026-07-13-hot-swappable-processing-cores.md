# Hot-swappable processing cores

Status: active

## Goal

Publish immutable, enumerable `mib_processing` releases and let MIB Studio
prepare and explicitly activate an approved native core at a quiescent
between-operation boundary. Biowork resolves the same registry into isolated
per-version Python environments. No application/image redeployment is required
to use a newly published core, and every run records exact provenance.

GitHub tracking:

- [MIB Studio epic #236](https://github.com/KPT1020/mib-studio-qt/issues/236)
- [Biowork companion epic #111](https://github.com/gavinlouuu-kpt/Biowork-monorepo/issues/111)
- [Project #6](https://github.com/users/gavinlouuu-kpt/projects/6)

## Acceptance criteria

- [ ] A signed version is published once as immutable metadata and appears in
      the channel catalog without destroying prior versions.
- [ ] `latest.json` remains consumable by legacy clients; exact pins resolve
      through `versions/<version>.json` and fail closed.
- [ ] Live capture, experiments, replay overlays, offline regeneration,
      tracking/target selection, and empty-frame detection use one selected
      kernel implementation.
- [ ] Native activation verifies SHA-256, Authenticode signer, ABI, contract,
      runtime, app compatibility, and self-test before transactional swap.
- [ ] A core remains pinned for an operation; activation is rejected while any
      processing lease, experiment, recording, preview executor, or batch job
      is active.
- [ ] Profiles declare contract compatibility but never select a core; HDF5
      output records core version, contract/ABI, artifact/manifest digests,
      release tag, and source.
- [ ] Biowork can run the baked baseline or an exact compatible release wheel
      from a persistent, lock-guarded environment cache.
- [ ] Unit, conformance, lifecycle/stress, HDF5, docs, Windows signing, and live
      hardware acceptance gates pass.

## Delivery slices and dependencies

- [x] [A6 #238](https://github.com/KPT1020/mib-studio-qt/issues/238) — add
      schema-v2 canonical identity, immutable history, short-cache catalog,
      PEP 503 page, GitHub Release asset derivation, safe merge/idempotency,
      and atomic version bump/tag tooling. Unit and sandbox dry-run coverage is
      implemented; real R2 proof remains A12.
- [ ] [A7 #242](https://github.com/KPT1020/mib-studio-qt/issues/242) — route
      every scientific path through `IProcessingKernel`, preserving golden
      behavior before introducing dynamic loading.
- [ ] [A8 #239](https://github.com/KPT1020/mib-studio-qt/issues/239) — freeze
      the POD/opaque-handle C ABI, build the Windows plugin and descriptor, and
      validate independently built compatible/incompatible fixtures. Local/CI
      wiring now includes separately configured C/C++ fixture modules,
      import/export audit, ephemeral signing tests, and isolated Production
      signing; the real signed run remains A12 evidence.
- [x] [A9 #237](https://github.com/KPT1020/mib-studio-qt/issues/237) — extend
      the existing conformance-gated tag workflow through Python 3.13, build
      and sign the native asset, attach one release set, then publish immutable
      manifest/catalog/package page before `latest.json`. The production
      signing/R2 secret path is wired but intentionally not claimed as run.
- [ ] [A10 #241](https://github.com/KPT1020/mib-studio-qt/issues/241) — add the
      trusted content-addressed cache, catalog client, resident module loader,
      core leases, and transactional activation manager. A→B→A, rejection-before-
      reset, stale-state clearing, and watchdog concurrency stress are green;
      the Windows live rehearsal remains.
- [ ] [A11 #243](https://github.com/KPT1020/mib-studio-qt/issues/243) — add the
      Processing Core selector/status, admin pin, compatibility handling, and
      HDF5/run provenance. Profile contract compatibility, explicit downgrade,
      and recorded-vs-active regeneration warnings are now implemented; live
      signed selector/restart evidence remains.
- [ ] [A12 #240](https://github.com/KPT1020/mib-studio-qt/issues/240) — publish
      a real signed release to R2, activate/downgrade it on Windows hardware,
      and capture performance/provenance evidence.

A6 and A7 proceed in parallel. A8 depends on A7. A9 depends on A6+A8; its
workflow can land before A8 while the new target is developed on the same
branch. A10 depends on A6+A8, A11 on A7+A10, and A12 is the live exit gate.
Biowork B8 starts from A6's contract; its version manager and launcher follow
the resolver and remain independent of desktop activation internals.

## Public contracts

- Registry keys are `{channel}/processing-core/latest.json`, immutable
  `versions/<version>.json`, `index.json`, and
  `simple/mib-processing/index.html`.
- `latest.json` is the canonical channel-active pointer and is written last;
  `index.active_version` is an enumerable mirror, never sufficient by itself
  to select Active during a partially completed publication.
- Schema v2 keeps schema-v1 wheel/contract/profile/LUT fields and adds canonical
  `version`, wheel filename/size, and optional `native_plugins[]`.
- A native release is
  `mib_processing_core-<version>-windows_x86_64.{dll,json}`. The sidecar names
  the ABI/contract/runtime/app/signing claims; the publisher derives URL,
  digest, and size from actual release bytes.
- The plugin exports `mib_processing_get_api`; no Qt/OpenCV/STL/exceptions or
  allocator ownership crosses its versioned C boundary.
- Environment exact pins outrank user selection. Channel publication is only
  a recommendation and never silently activates a desktop core.

## Verification

- Registry/version tooling: `python3 -m unittest -v
  test_publish_processing_core.py test_bump_mib_processing_version.py
  test_contract_version_consistency.py test_publish_profiles.py`.
- Docs/vault: `python3 scripts/check_docs.py`.
- Backend/core: Linux backend CTest plus installed-wheel and dynamic-plugin
  golden conformance.
- Shared-state changes: watchdog-protected stress, frame-accounting invariants,
  and TSan.
- Save/provenance changes: HDF5 round-trip plus fault injection and legacy-file
  compatibility.
- Realtime boundary: steady-state median/p95 ratio against the bundled adapter;
  no loading, hashing, signature work, or context creation per frame.
- Release: workflow parse, Windows x64 build/import audit, valid Authenticode
  signature, descriptor/manifest equality, exact desktop installer artifacts,
  then live R2/hardware proof in A12. Stable manual CI may publish refs only
  after build/test/package gates; manual tag dispatch must build the exact tag.
  Every desktop publisher must read back the configured numeric/full identity,
  run CTest before external mutation, and preserve full beta identity in R2.

## Decision log

- 2026-07-13: Native desktop cores use in-process C ABI plugins; Biowork keeps
  per-version Python environments because its runtime already speaks Python.
- 2026-07-13: Switching is only between operations. The host owns threads and
  HDF5; the selected kernel owns all version-sensitive scientific behavior.
- 2026-07-13: New publications never auto-activate. Explicit/admin pins fail
  closed; an unpinned fresh install keeps the bundled core.
- 2026-07-13: Native trust requires manifest SHA-256 plus an approved
  Authenticode signer. Sidecar signing metadata is informational, not a trust
  root.
- 2026-07-13: `latest.json` stays a full document for backward compatibility;
  immutable history plus `index.json` provides exact selection and rollback.
- 2026-07-13: rollback copies the existing immutable bytes via
  `--promote-version`; it never reconstructs an old version using current
  serializer/schema behavior.
- 2026-07-13: `bindings/python/pyproject.toml` is the wheel version source of
  truth. Tag creation is a post-commit step so it cannot point at stale files.
- 2026-07-14: Native selection persistence is a locked activation pre-commit,
  not post-swap cleanup. A failed settings sync preserves the prior live core.
  The desktop uses the stable `MIB Studio` settings namespace and performs a
  one-time, non-destructive migration of every legacy `Unknown Organization`
  key before reading any preferences; an unwritable migration stops startup.
- 2026-07-14: Every maintained desktop publisher must opt into CMake's
  non-empty 64-hex signer requirement using the GitHub repository SPKI
  variable. Native release CI independently derives the signed DLL's DER-SPKI
  hash and rejects a mismatch before upload; ordinary development/fork builds
  keep the requirement disabled.
- 2026-07-14: Desktop releases publish only exact, freshly built numeric-version
  installer paths. Manual stable CI defers and atomically publishes its version
  commit/tag after all build gates; tag dispatch validates, checks out, and
  resolves the requested tag rather than inheriting the workflow branch tip.
- 2026-07-14: Desktop version bumps start from the highest reachable release
  tag line, not only the fallback literal. Paired one-configure CMake overrides
  make numeric/full binary identity explicit and reviewable. Beta installer
  bytes retain numeric Inno/GitHub names but use full-version immutable R2 keys;
  equal numeric SHA betas order by publication time.

## Residual infrastructure gates

- Production certificate material and the approved signer identity remain
  unavailable. R2 access is verified and stable LUT revision `2026.07.14-1`
  is publicly reachable, but no fake signing identity was invented and no
  unsigned core release was attempted.
- The real GitHub tag/release, conditional R2 reads/writes, and package-page
  installation must be demonstrated in A12.
- PowerShell and the Windows packaging toolchain are unavailable in this Linux
  sandbox. Static regression tests and YAML parsing cover the release wiring,
  but both stable and beta entrypoints still require a real Windows-runner
  rehearsal before production release.
- Windows microscopy hardware and a published v2 do not exist here. A local
  Docker daemon can validate Biowork's image/UI wiring, but cannot prove the
  release-driven version swap without that signed release; unit/conformance
  wiring is not a substitute for live evidence.
- A7 remains a deliberate architectural blocker: ABI v1 owns mask/empty-frame
  decisions, while contours, scientific metrics, tracking, target selection,
  and callbacks remain host-owned. A science-changing core is not fully
  swappable until that boundary is redesigned and re-baselined.
