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
- [ ] Registry and loader contracts remain OS-neutral: platform-matched shared
      libraries carry a mandatory named trust scheme, while an unimplemented
      platform verifier fails closed rather than falling back to unsigned code.
- [ ] `latest.json` remains consumable by legacy clients; exact pins resolve
      through `versions/<version>.json` and fail closed.
- [ ] Live capture, experiments, replay overlays, offline regeneration,
      tracking/target selection, and empty-frame detection use one selected
      kernel implementation.
- [ ] Native activation verifies SHA-256, the required platform signer, ABI,
      contract, runtime, app compatibility, and self-test before transactional
      swap.
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
      behavior before introducing dynamic loading. The C++ seam is now total:
      contours/metrics/LUT/target gating (`analyzeObjects`) and batch track
      matching (`matchTrack`) execute through the selected kernel with the
      shared implementation in `ProcessingScience.cpp`, pinned by a
      pre-migration golden test and a spy-kernel routing proof. Remaining: an
      ABI v2 that marshals object records across the C plugin boundary so a
      dynamic core can replace the science, plus overlay-contour unification.
- [ ] [A8 #239](https://github.com/KPT1020/mib-studio-qt/issues/239) — freeze
      the POD/opaque-handle C ABI, build the Windows plugin and descriptor, and
      validate independently built compatible/incompatible fixtures. Local/CI
      wiring now includes separately configured C/C++ fixture modules,
      import/export audit, a trusted signed-SDK verifier matrix, and isolated Production
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
- [ ] [A13 #245](https://github.com/KPT1020/mib-studio-qt/issues/245) — add the
      Linux `.so` release/audit lane and an offline-verifiable detached-signature
      adapter behind the same injected trust boundary. The Ed25519 verifier,
      compiled SPKI pin, manifest signature transport, hidden-visibility `.so`
      + sidecar build, and the Linux CI build/audit/sign-rehearsal lane are
      implemented and tested; the live gate (production keypair, repository
      pin, Production signing job, signed `.so` publication, distro baselines)
      remains open and unsigned Linux production activation stays unavailable.

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
- A current native release is
  `mib_processing_core-<version>-windows_x86_64.{dll,json}`. The registry also
  reserves OS-matched `.so`/`.dylib` artifacts without changing schema. The
  sidecar names ABI/contract/runtime/app and mandatory signing-scheme claims;
  the publisher derives URL, digest, and size from actual release bytes.
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
- 2026-07-14: The C ABI, immutable registry, content-addressed cache, and module
  loader are platform contracts. Authenticode is only the Windows trust
  adapter. Linux `.so` metadata is accepted now, but production activation
  stays fail-closed until A13 implements and audits a detached-signature scheme.
- 2026-07-14: The Linux trust adapter is a detached Ed25519 signature over the
  exact artifact bytes, transported in the immutable manifest's `signing`
  block (44-byte DER SPKI + 64-byte raw signature, both base64) and trusted
  only against the compiled `MIB_PROCESSING_CORE_ED25519_SPKI_SHA256` pin —
  the same pin format as the Windows Authenticode SPKI. The publisher
  re-derives the key hash from the key bytes; manifest fields are transport,
  never a trust root. An empty pin, a non-Ed25519 key, or any non-canonical
  encoding fails closed. Envelope, rotation, and revocation policy:
  `docs/architecture/processing-core-linux-signing.md`.
- 2026-07-14: A7's seam expansion is code motion behind `IProcessingKernel`,
  not a rewrite: `ProcessingScience.cpp` holds the single science
  implementation, `analyzeObjects`/`matchTrack` are kernel virtuals whose
  defaults execute it, and `ProcessingService` only routes. A golden test
  (`processing.science_golden`) was pinned against the pre-migration outputs
  first; a spy kernel (`processing.science_seam`) proves batch/offline
  routing. The native plugin now compiles the science sources so the released
  artifact is self-contained; ABI v1 still transports mask/empty only.

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

## Worker pass-off — 2026-07-14 15:33 HKT

### Workspace and review state

- Branch in both repositories:
  `claude/biowork-pipeline-portability-j3qpu6`.
- MIB worktree: `/home/gavin/Developer/.worktrees/mib-hot-swap`.
  Implementation head before this documentation-only hand-off commit is
  `9e22e8f`; PR
  [#244](https://github.com/KPT1020/mib-studio-qt/pull/244) remains draft.
- Biowork worktree: `/home/gavin/Developer/.worktrees/biowork-hot-swap`.
  Head `26e9573`; PR
  [#118](https://github.com/gavinlouuu-kpt/Biowork-monorepo/pull/118)
  remains draft and all reported checks are green.
- Both worktrees were clean before writing this pass-off. Do not merge or mark
  either PR ready while the live gates below remain open.

### Completed and locally verified

- `3f55ec1` makes native registry/selector identity OS-neutral: OS-matched
  `.dll`/`.so`/`.dylib`, normalized `x86_64`/`aarch64`, mandatory named trust
  schemes, persisted trust identity, explicit Linux runtime fingerprints, and
  fail-closed non-Windows activation pending A13.
- `9e22e8f` removes hosted-runner certificate generation and trust-store writes
  from PR CI. The Windows verifier matrix copies the embedded-signed Windows
  SDK `signtool.exe`, derives its signer SPKI, and passes unsigned/valid/wrong-
  signer/tamper cases to the watchdog-bounded C++ WinVerifyTrust verifier.
  Production signing of the actual core remains a separate secret-backed job.
- Local verification passed: 42 Python registry/version tests; all 68 Linux
  backend/UI CTests (hardware-only and remote Dataset Viewer cases skip by
  design); PowerShell syntax parsing in the official PowerShell container;
  docs/vault integrity; workflow YAML parse; and `git diff --check`.
- E2E screenshot evidence remains under `/tmp/mib-e2e-screenshots/` and
  `/tmp/biowork-e2e-screenshots/`; it is local evidence, not a committed
  release artifact.

### Active CI proof to pick up first

Implementation run
[29314789785](https://github.com/KPT1020/mib-studio-qt/actions/runs/29314789785)
was active at hand-off. Windows job `87026251950` was in Conan dependency
installation; Docs CI and desktop workflow validation were already green.
Poll it with:

```bash
cd /home/gavin/Developer/.worktrees/mib-hot-swap
gh run view 29314789785 --json status,conclusion,jobs
gh api repos/KPT1020/mib-studio-qt/actions/jobs/87026251950 \
  --jq '{status,conclusion,current:[.steps[]|select(.status=="in_progress")|.name]}'
```

If the Windows job fails, collect only the bounded failure before changing
code:

```bash
gh run view 29314789785 --job 87026251950 --log-failed
```

Do not reintroduce certificate creation or Root-store mutation. Diagnostic
runs `29313197025` and `29313984541` proved `certutil` stalls after
`Signature matches Public Key` for both a self-signed leaf and a proper
CA-root/code-signing-leaf chain. The current signed-SDK sample deliberately
avoids that hosted-image defect.

On a green result, update PR #244 and A8/#239 with the run/job links and exact
WinVerifyTrust matrix evidence. A documentation-only hand-off commit may mean
the run is attached to its immediate parent rather than the new head; record
that relationship explicitly rather than claiming an unrun commit.

### Gates that must stay open

- A7/[#242](https://github.com/KPT1020/mib-studio-qt/issues/242): the C++
  kernel seam now owns contours, metrics, LUT, target gating, and track
  matching (golden-pinned, spy-verified), but the C ABI v1 still transports
  only mask/empty decisions — ABI v1 dynamic cores inherit the host-compiled
  default science. Full swappability of a science-changing core requires the
  ABI v2 object-record marshalling and re-baselining; callbacks and track
  lifecycle state deliberately stay host-owned.
- A12/[#240](https://github.com/KPT1020/mib-studio-qt/issues/240): provision
  the production certificate/password and repository SPKI pin; define branch
  protection and Production-environment approval rules; publish a real tag/R2
  version; and prove signed A→B→A activation, restart, provenance, latency, and
  hardware behavior. The 2026-07-14 API audit found unprotected `main` and no
  Production protection rules.
- A13/[#245](https://github.com/KPT1020/mib-studio-qt/issues/245): the
  envelope/rotation/revocation policy, Linux Ed25519 verifier, and CI
  build/audit/sign-rehearsal lane are implemented
  (`docs/architecture/processing-core-linux-signing.md`). Still open live:
  provision the production Ed25519 keypair and repository SPKI variable, add
  the Production-gated signing job and release-asset allowlist entries, and
  build/sign/publish a real `.so` across supported distro baselines. An
  unpinned production Linux build correctly fails closed.
- Biowork B13/[#116](https://github.com/gavinlouuu-kpt/Biowork-monorepo/issues/116):
  run live v1/v2 concurrent triggers, cold/warm cache checks, MLflow evidence,
  rollback, and the real auto-bump PR against a published core.

Issue #236 now includes A13 and platform-neutral trust wording. A13's landed
versus residual audit is recorded in
[#245](https://github.com/KPT1020/mib-studio-qt/issues/245#issuecomment-4966396755),
and the release-governance evidence is recorded in
[#240](https://github.com/KPT1020/mib-studio-qt/issues/240#issuecomment-4966314630).
