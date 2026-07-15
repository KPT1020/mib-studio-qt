# Portable Processing Conformance Harness

Status: completed — implementation and pinned real-corpus evidence verified

## Goal

Finish issue #225 / epic #220 with a standalone anti-drift harness that runs
the shipped `mib-processing` wheel, compares its full portable output against
a committed deterministic reference, and fails CI on metric, mask,
multi-image-series, target-group, or tracking drift.

## Acceptance criteria

- [x] `scripts/compare_metrics.py` compares every portable metric plus optional
      Young's-modulus, target-group/tracking metadata, mask hashes, and series
      image hashes, with unambiguous multi-object record matching.
- [x] A standalone runner produces a schema-valid candidate from the installed
      wheel and exits non-zero on any drift from the committed reference.
- [x] The wheel returns target-group/tracking metadata and populated series
      images when requested.
- [x] `.github/workflows/python-wheel.yml` runs the harness against the wheel
      it just built and installed.
- [x] The user-supplied private `gavinlouuu/z_adjustment-data` corpus replaces
      the unavailable PANC1 input for real-HDF5 release evidence; the command,
      Hub revision, source SHA-256, bounded window, and committed output
      reference are documented. The deterministic reference keeps CI runnable
      without private experiment data.
- [x] Tests and vault notes are updated and repository verification passes.

## Decision log

- 2026-07-13: Store masks and series images as SHA-256 digests in the JSON
  conformance record. This proves byte parity without committing large binary
  arrays or experiment data.
- 2026-07-13: Use a generated deterministic ring-frame sequence for the
  always-on CI reference because the designated PANC1 HDF5 file is not in the
  repository and runtime experiment data must stay under ignored `data/`.
  Keep a separate documented command for validating a local copy of PANC1.
- 2026-07-13: Add new parity fields as optional schema-v1 properties. Existing
  metrics-only JSON remains valid; the conformance runner requires the richer
  fields in its own committed reference.
- 2026-07-13: Searched the workspace, user home, and mounted volumes for
  `PANC1 PDE3A CONTROL.h5`; no copy was available. The user supplied private
  Hugging Face dataset `gavinlouuu/z_adjustment-data` as the replacement. Pin
  the 50V in-focus file by Hub revision + LFS SHA-256 and use frames 500–507,
  which exercise valid multi-object tracks and non-empty series payloads.

## Progress

- [x] Inspect issues #220-#225 and existing A1-A4 implementation.
- [x] Add regression coverage for series and target/tracking metadata.
- [x] Implement full-parity bindings, reference generator, and comparator.
- [x] Wire wheel CI and CTest.
- [x] Update docs/vault and verify.
- [x] Run the harness against the supplied real HDF5 corpus: 8 inputs produce
      12 records, including 6 valid object records and 6 non-empty series; an
      exact rerun passes and an altered mask hash exits 1.

## Verification

- Source HDF5 byte count: `1519856704`; SHA-256 exactly matches the Hub LFS
  object `7ed2a721c85adc600688e32d4c4dbb7a58f0c725e351559ebc946eddab311bdc`.
- HDF5 layout: `/recorded_frames/images` is `(30878, 96, 512) uint8`.
- Dataset-backed reference rerun: 12/12 records matched with no differences.
- Intentional `mask_sha256` drift: comparator reported one differing frame and
  exited 1.
- Script/unit suites, wheel binding pytest, JSON/YAML parsing,
  `git diff --check`, and `python3 scripts/check_docs.py` pass.
