# Processing Core Dialog

> Settings UI for enumerating, preparing, and activating native
> deformability-cytometry processing-core versions.

**Source:** `src/frontend/dialogs/ProcessingCoreDialog.cpp`,
`include/frontend/dialogs/ProcessingCoreDialog.h`,
`src/frontend/utils/ProcessingCoreCatalog.cpp`,
`include/frontend/utils/ProcessingCoreCatalog.h`
**Related:** [[MainWindow]], [[Dialogs]],
[[../services/ProcessingService]], [[../architecture/Threading-Model]],
[[../build-and-run/Build]]

## User flow

Open **Settings → Processing Core…**, choose the stable or beta channel, then
select **Prepare & Activate**. The list marks the channel-active version from
the independently fetched `latest.json` pointer, the
currently selected version, and entries incompatible with the current OS,
architecture, or app-version range. The status bar always shows the active
core version and contract.

Activation is a between-operation change. The dialog prepares and validates a
candidate, but `ProcessingService` refuses the final swap while realtime,
capture-backed experiment work, async batch work, or an offline synchronous
batch owns the current core. Stop those operations and retry.

## Resolution and trust chain

1. Fetch `{base}/{channel}/processing-core/index.json` over HTTPS with a
   20-second timeout and parse the schema-v1 history index defensively.
2. Fetch and validate `{base}/{channel}/processing-core/latest.json`
   independently. Its manifest version is the sole channel-active pointer;
   `index.active_version` is advisory and a disagreement produces a partial-
   publication warning rather than leading the selector.
3. Fetch the selected version's immutable schema-v2 manifest over HTTPS,
   hash its raw bytes, and cross-check its identity and native artifact fields
   against the mutable index.
4. Download the platform DLL as bounded 64 KiB chunks over HTTPS with a
   120-second absolute deadline; verify its declared byte size and SHA-256.
5. Materialize it atomically at
   `<cache>/<version>/<sha256>/<filename>`, guarded by a directory lock with
   stale-lock recovery. `.ready.json` is written last.
6. Immediately before module load, verify SHA-256 again, validate the embedded
   Authenticode chain, require the signer public key's DER SPKI SHA-256 compiled
   into the application, use restricted Windows DLL
   dependency search, negotiate ABI/contract/runtime identity, and run the
   plugin self-test.
7. Activate and persist the verified identity/path in
   `QSettings`. Startup restores that cached artifact through the same
   digest/trust/ABI checks before capture begins.

Production native loading is Windows-only. Non-Windows trust verification
fails closed. Debug builds alone may set
`MIB_STUDIO_ALLOW_UNSIGNED_PROCESSING_CORE=1` for local loader fixtures.

## Configuration

- `MIB_STUDIO_PROCESSING_CORE_BASE_URL` — registry base; default
  `https://updates.yofo.bio`.
- `MIB_STUDIO_PROCESSING_CORE_CACHE_DIR` — absolute persistent cache root;
  default is the app-local data directory's `processing-cores` folder.
- `MIB_PROCESSING_CORE_SIGNER_SPKI_SHA256` — CMake cache value compiled into
  the production app as the approved Authenticode signer public-key hash.
  Release builds fail closed when it is absent. Debug builds alone can use
  `MIB_STUDIO_PROCESSING_CORE_SIGNER_SPKI_SHA256` as a local override.
- `MIB_STUDIO_PROCESSING_CORE_VERSION` — administrator hard pin. Other
  versions are disabled and experiment start fails closed until the pinned
  core is active.

## Known boundary

The engine ABI v1 selects mask generation and empty-frame classification.
Metrics, contour filtering, tracking, target decisions, and orchestration
remain host-owned and consume the selected mask. A version that changes those
semantics is not fully hot-swappable yet; GitHub issue #242 (A7) remains open.
Independent Windows ABI-fixture auditing (#239/A8), A→B→A/TSan and persistence-
rollback stress (#241/A10), and profile/review/downgrade UI policy (#243/A11)
also remain explicit follow-ups.
Production signing/R2 publication, compiling the real approved SPKI, and an
on-hardware Windows exercise are live-environment gates tracked under A12.
