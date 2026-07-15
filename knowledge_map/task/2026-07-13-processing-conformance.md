# Portable Processing Conformance (Issue #225)

## Outcome

`scripts/run_processing_conformance.py` runs the installed `mib-processing`
wheel on a deterministic nested-contour sequence and compares it with
`scripts/gold_standard_dataset.json`. The candidate/reference carry all
portable metrics plus target-group/tracking metadata and SHA-256 evidence for
each mask and ordered multi-image-series frame. Missing/extra records and any
value or byte drift fail with a non-zero exit.

The same runner was verified against the pinned 50V in-focus HDF5 window from
`gavinlouuu/z_adjustment-data`: 8 source frames produced 12 records, including
6 valid tracked objects with non-empty series payloads. An exact rerun matched
12/12; an intentional mask-hash change exited 1.

## Decisions

- Keep the always-on fixture generated and tiny; experiment HDF5 files remain
  ignored runtime data under `data/`.
- Use the supplied private `gavinlouuu/z_adjustment-data` 50V in-focus
  recording as real-corpus release evidence, pinned by Hub revision and source
  LFS SHA-256. Commit only metrics and output hashes, never the 1.52 GB input.
- Hash dtype + shape + bytes, not bytes alone, so geometry drift cannot collide
  with an equal byte stream.
- Match multi-object records by `(index, frame_type, object_id)` and reject
  duplicate identities. Comparing only frame index silently dropped duplicate
  object records.
- Optional parity fields preserve schema-v1 compatibility with older
  metrics-only exports, but a field present in the reference is mandatory in
  the candidate.
- Enforce conformance fixture identity and source-frame count at document
  level; output equality alone must not allow the wrong corpus window to pass.

## Implementation notes

- `ProcessingService::processBatch` now fills `seriesImages` for newly retained
  valid tracks when multi-image config is enabled; the Python API's existing
  `include_series_images=True` flag no longer returns an always-empty list.
- Python result dicts now expose `is_target_group` plus track ID/span/count.
- `compare_metrics.py` covers `youngs_modulus`, object identity, optional parity
  fields, document/contract metadata, and candidate-only records.
- Wheel CI uploads its generated candidate artifact for diagnosis.
- Local macOS verification surfaced and fixed conditional OpenCV 5 geometry
  linkage/header and upstream/Homebrew HDF5 2.x target-name compatibility.

## External gold dataset

The designated `PANC1 PDE3A CONTROL.h5` was unavailable. The user supplied the
private Hugging Face `gavinlouuu/z_adjustment-data` corpus instead, so the
runner now reads a bounded frame window directly from HDF5. The committed
`scripts/z_adjustment_50v_reference.json` pins eight frames at offset 500 of
the 50V in-focus file at Hub revision
`fc62e3147fb0237e46e6eebd0fb09e669abef12f`. That window was selected because
it exercises valid multi-object tracks and non-empty series payloads;
`scripts/test_run_processing_conformance.py` protects the bounded loader.

## Related

- [[../services/ProcessingService]]
- [[../domain/Microscopy-Pipeline]]
- [[../build-and-run/Build]]
