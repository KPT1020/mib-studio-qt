# Processing Contract v2

Status: active

## Goal

Ship Processing Contract v2 — a new, explicitly versioned scientific pipeline
that uses `cv::absdiff` for background comparison, adds a deterministic
preprocessing filter stage, and replaces ring width with a per-detected-object
Laplacian variance focus metric — while keeping Contract v1 byte-for-byte
reproducible. Every profile, native core, Python wheel, and HDF5 file declares
and is matched on its contract, and legacy artifacts remain readable without
presenting ring width as a current v2 metric.

GitHub tracking:

- [Epic #296](https://github.com/KPT1020/mib-studio-qt/issues/296)
- V2-1 [#297], V2-2 [#298], V2-3 [#299], V2-4 [#300], V2-5 [#301],
  V2-6 [#302], V2-7 [#303]
- Builds on hot-swappable cores epic [#236] and [#242].

See [ADR 0001](../../decisions/0001-processing-contract-v2.md) and the
[compatibility matrix](../../architecture/processing-contract-compatibility.md).

## Acceptance criteria (epic)

- [ ] Contract 1 golden outputs are byte-for-byte unchanged.
- [ ] Contract-1 and Contract-2 profiles execute only with compatible
      implementations.
- [ ] Existing schema-1 files are never silently rewritten with schema-2 keys.
- [ ] Copy-upgrade preserves unrelated values and removes all live ring config.
- [ ] v2 uses `cv::absdiff` and one shared difference path across realtime,
      batch, HDF5 reanalysis, empty filtering, and auto-background.
- [ ] Laplacian variance is computed per detected object only; N objects → N
      independent values; masking applied to statistics, not before convolution.
- [ ] v2 autofocus maximizes an object focus score; no ring terminology.
- [ ] ABI v1 and v2 coexist and negotiate explicitly.
- [ ] HDF5/Python/C++/JSON/CSV agree on names, units, ordering, NaN handling.
- [ ] Calibrated defaults with the Laplacian gate disabled until reviewed.

## Delivery slices and dependencies

| Slice | Issue | Depends on | Summary |
|-------|-------|-----------|---------|
| V2-1 | #297 | — | Schema, versioning, migration, compatibility boundary |
| V2-2 | #298 | V2-1 | Preprocessing filters + shared absdiff difference path |
| V2-3 | #299 | V2-1, V2-2 | Remove ring width; per-object Laplacian variance |
| V2-4 | #300 | V2-3 | Focus-score autofocus controller |
| V2-5 | #301 | V2-1, V2-2, V2-3 | Engine ABI v2 (filters + full per-object results) |
| V2-6 | #302 | V2-1, V2-3, V2-5 | HDF5/Python/export/profiles/UI migration |
| V2-7 | #303 | V2-2…V2-6 | Calibration + validation (release gate) |

Branches are stacked in this order (`claude/pc2-v2-1-schema` → … ).

## Decision log

- 2026-07-21: Two version axes (`processing_contract_version`,
  `config_schema_version`), both bumped to `2` for v2; matched by equality, not
  ordering (ADR 0001).
- 2026-07-21: v2 migration logic lives Qt-free in `mib_processing`
  (`backend::processing::contract`, nlohmann::json) so it is exercised by the
  backend-only CTest lane; the Qt `ProfileManager` calls into it.
- 2026-07-21: Canonical v2 difference key is `difference_threshold`;
  `bg_subtract_threshold` is accepted only through the compatibility adapter.
- 2026-07-21: Migrated profiles get an identity preprocessing chain and a
  disabled Laplacian gate; the gate stays disabled until calibrated in V2-7.

## Progress

### V2-1 — schema, compatibility, migration boundary
- [x] ADR 0001 + compatibility-matrix doc.
- [x] Qt-free contract module: version constants, schema classifier, canonical
      difference-threshold adapter, v1→v2 migrator.
- [x] Backend unit tests (migration, canonical/legacy key, classifier,
      fail-closed).
- [x] Schema-aware `ProfileManager` loading (`normalizeConfigForSchema` fails
      closed on unknown schema; `copyUpgradeConfigToV2` bridges to the migrator).
- [x] Vault notes updated; `check_docs.py` green.

### V2-2 — preprocessing filters + shared absdiff path
- [x] Qt-free `ImageFilterPipeline` (identity/invert/linear_contrast/gamma/
      clahe), compiled once, fail-closed on unknown/invalid stages.
- [x] One shared `buildDifferenceImage` (+ cropped variant): input filters
      symmetric, contract-gated absdiff vs subtract, difference filters,
      incompatible-background error under v2, ROI zero-outside preserved.
- [x] Bundled kernel `processMask` + `isEmpty` and host `isFrameEmpty` helpers
      routed through the one helper. Contract-1 output unchanged.
- [x] Tests (`processing.image_filter_pipeline`) + vault. Golden/seam/
      multi-object regression green.
- [ ] Real preprocessing stages fed from a v2 config/ABI (deferred to V2-5/V2-6;
      pipelines are identity until then).

### V2-3…V2-7
- [ ] Not started (stacked branches created as each slice begins).
