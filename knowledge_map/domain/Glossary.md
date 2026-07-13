# Glossary

> Domain and code terms you'll run into. Short definitions; follow
> `[[WikiLinks]]` for detail.

## Microscopy / measurement

- **Deformability** — shape deviation from a perfect circle. Computed in
  [[../services/ProcessingService]] from contour moments. Low = round,
  high = elongated.
- **Area** — contour pixel count converted to μm² using
  `pixelToMicronFactor_` (default 0.4886 μm/px).
- **Ring ratio** — ratio of outer/inner contour radius. Used for focus
  feedback — drives [[../services/AutofocusService]] via
  `RingRatioCallback`.
- **Brightness quantiles** — Q1/Q2/Q3/Q4 percentiles of pixel intensity
  within the contour mask. Part of `FilterResult`.
- **Young's modulus (E-modulus)** — stiffness. Computed by LUT lookup from
  `(area, deformability)`; LUT loaded from
  `resources/isoelastic_curve/*.txt` by `AppBackend::initialize`.
- **Target group** — a second gate within valid frames; frames matching
  target-group criteria pulse the camera trigger
  ([[../services/TriggerService]]).
- **ROI** — rectangular region of interest; applied pre-analysis by
  [[../services/ProcessingService]] and by display in [[../frontend/PreviewPage]].

## Portability

- **Portable processing contract** — the frozen combination of the
  gold-standard metrics JSON shape, `ProcessingConfig` JSON shape, and
  Young's-modulus LUT text format that a non-Qt consumer (e.g. Biowork's
  `services/mib-processing`) must match to get identical results. Defined in
  `docs/gold_standard_metrics.md` ("Portable Processing Contract" section)
  and `docs/gold_standard_metrics.schema.json`.
- **`contract_version`** — single integer naming one frozen version of the
  portable processing contract above. Bump it whenever the metrics schema,
  config schema, or LUT format changes incompatibly. Currently declared
  independently in six places (`test_contract_version_consistency.py` guards
  against drift until/unless that's folded into one source of truth).
- **Processing core manifest** — `{channel}/processing-core/latest.json`,
  published by `publish-processing-core.py`. Pins the `mib-processing` wheel
  version + `contract_version` together and cross-links the profile catalog
  and emodulus LUT manifest, so a consumer resolves config + LUT + engine as
  one set. See `docs/portable-processing-sync.md`.
- **Processing conformance reference** —
  `scripts/gold_standard_dataset.json`, a deterministic full-parity output from
  the installed wheel. `scripts/run_processing_conformance.py` fails on metric,
  mask, series-image, target-group, tracking, or record-accounting drift.

## Protocols & SDKs

- **GenICam** — standard camera control API.
- **SFNC** — GenICam Standard Features Naming Convention. Features like
  `DeviceReset` are SFNC-defined.
- **PFNC** — GenICam Pixel Format Naming Convention. Codes used in
  `Frame::pixelFormat`.
- **Euresys EGrabber** — SDK for CoaXPress framegrabber + cameras. See
  `egrabber-sample-programs/` and `docs/integration/egrabber.md`.
- **StreamModule** — EGrabber statistics module
  (`StatisticsFrameRate`, `StatisticsDataRate`). Must refresh before
  stopping capture.
- **Modbus RTU** — serial protocol used by
  [[../services/SyringePumpService]] (Sample + Sheath pumps).
- **Coremor XMT** — serial protocol for the piezo nanopositioner used by
  [[../services/AutofocusService]]. DLL under `include/Coremor/`.
- **ONNX Runtime** — ML runtime for [[../services/YoloService]].

## Code idioms

- **PIMPL** — "pointer to implementation" pattern. Hides third-party
  headers (HDF5, ONNX). Used by [[../services/Hdf5Service]] and
  [[../services/RecorderService]].
- **Absolute write index** — monotonic 64-bit counter in
  [[../data-model/FrameStore]]. Wraps only if the ring does, but indices
  themselves never decrease.
- **FrameStore filter mode** — frame filter that returns `true` to SKIP
  (used by recording mode to drop empty frames).
