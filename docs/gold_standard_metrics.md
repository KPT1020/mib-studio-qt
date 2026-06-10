# Gold Standard Metrics

Uniform JSON format for processing pipeline metrics so **mib-studio-qt** pipeline output can be compared against the **gold standard** (MIB-Studio legacy pipeline).

## File format

- **Schema**: `gold_standard_metrics.schema.json` (referenced but never committed; see tech debt TD-6 in [exec-plans/tech-debt-tracker.md](exec-plans/tech-debt-tracker.md))
- **Top-level fields**:
  - `version`: Schema version (currently `1`).
  - `pixel_to_micron`: Conversion factor (1 pixel = X microns). Required for area in µm².
  - `source`: Optional label (e.g. `mib-studio-qt`, `MIB-Studio-gold`).
  - `frames`: Array of per-frame metric objects.

## Per-frame fields (units and meaning)

| Field | Type | Units / meaning |
|-------|------|-----------------|
| `frame_type` | string | `"valid"` or `"invalid"` (passed validation or not). |
| `index` | integer | Frame index from acquisition (monotonic). |
| `timestamp_ns` | integer | Timestamp in nanoseconds. |
| `object_id` | integer | One-based object candidate within the source frame/ROI, ordered left-to-right then top-to-bottom; `-1` when no object candidate was selected. |
| `object_count` | integer | Number of object candidates emitted for the source frame/ROI. |
| `deformability` | number | 1.0 − circularity; dimensionless. |
| `area` | number | Hull area in **pixels**. |
| `area_um2` | number | Area in **µm²** (optional; = area × pixel_to_micron²). |
| `area_ratio` | number | Hull area / contour area; dimensionless. |
| `ring_ratio` | number | sqrt(outer_area − inner_area); ring metric. |
| `is_valid` | boolean | True if frame passed all validation checks. |
| `touches_border` | boolean | True if contour touches image border. |
| `has_single_inner_contour` | boolean | True if exactly one inner contour (informational; acceptance uses at least one nested contour). |
| `in_range` | boolean | True if area within configured min/max. |
| `inner_contour_count` | integer | Number of inner contours. |
| `brightness_q1` | number | 25th percentile brightness in masked region. |
| `brightness_q2` | number | 50th percentile (median) brightness. |
| `brightness_q3` | number | 75th percentile brightness. |
| `brightness_q4` | number | 100th percentile (max) brightness. |

Field names align with `FilterResult` and `BrightnessQuantiles` in [ProcessingService.h](../include/backend/processing/ProcessingService.h) and with the CSV header used by `export_hdf5.py` (snake_case in JSON).

### Processing pipeline (nested contours)

When `require_single_inner_contour` (or equivalent) is enabled, the pipeline keeps any object that has **at least one** nested (inner) contour. Batch metrics are computed per nested contour/object candidate. Frame-level realtime snapshots still expose the first selected object for compatibility. The field `has_single_inner_contour` is informational: it is true only when there is exactly one inner contour; acceptance is based on having at least one nested contour.

### Multi-object ROI records

Batch processing emits one metrics record per detected object candidate. Multiple records can therefore share the same `index` and `timestamp_ns`; use one-based `object_id` values from `1` to `object_count` to distinguish duplicate detections from the same source frame/ROI. Each object candidate is validated independently, so an edge-touching object can be rejected while another object in the same ROI remains valid.

Known failure modes:

- **Edge touch**: contours within two pixels of the ROI border are marked `touches_border` and rejected when border checking is enabled because the object may be cropped.
- **Overlapping halos**: touching or overlapping rings/halos can merge into one contour hierarchy. In that case the pipeline may emit fewer object records than the true object count or attach an inner contour to the wrong outer contour.

## Workflow (summary)

1. **Gold standard**: Run MIB-Studio pipeline → export CSV from saved data → convert CSV to gold-standard JSON with `scripts/convert_legacy_csv_to_json.py` (referenced but never committed; see tech debt TD-6).
2. **Qt pipeline**: Run mib-studio-qt → save experiment to HDF5 → export to gold-standard JSON with `export_hdf5.py --format json`.
3. **Compare**: Run [compare_metrics.py](../scripts/compare_metrics.py) on the two JSON files (with optional per-field tolerances).

## Gold standard test dataset

The designated gold standard test dataset for quality-control and regression testing is **PANC1 PDE3A CONTROL.h5**.

- **HDF5 path**: `c:\Users\gavin\data\2026_jan_chengdu\PANC1 PDE3A CONTROL.h5` (or set your copy path in [scripts/gold_standard_dataset.json](../scripts/gold_standard_dataset.json)).
- **Pixel-to-micron**: Use `-p 0.4886` (script default) unless the experiment uses a different value.

**Generate reference JSON** from this HDF5:

```bash
python scripts/export_hdf5.py -i "path/to/PANC1 PDE3A CONTROL.h5" -o data/gold_standard_export --format json -p 0.4886
```

**Compare a candidate** to the committed reference:

```bash
python scripts/compare_metrics.py data/gold_standard_export/metrics.json path/to/candidate_metrics.json
```

Or, if [compare_metrics.py](../scripts/compare_metrics.py) is configured with the default gold path, pass only the candidate:

```bash
python scripts/compare_metrics.py path/to/candidate_metrics.json
```

## End-to-end workflow (step-by-step)

### 1. Produce gold-standard metrics (MIB-Studio)

- Run the **MIB-Studio** (legacy) processing pipeline on your data so it saves results to disk (e.g. master files: `{condition}_images.bin`, `{condition}_masks.bin`, `{condition}_data.csv`, etc.).
- From the same saved data, run MIB-Studio’s metrics export so you get a CSV (e.g. the output of `calculateMetricsFromSavedData` or equivalent). That CSV may have columns like: `Batch`, `Condition`, `ImageIndex`, `Timestamp_us`, `Deformability`, `Area`, `RingRatio`, `Valid`, `Method`, `ProcessingConfig`.
- Convert that CSV to gold-standard JSON:
  ```bash
  python scripts/convert_legacy_csv_to_json.py -i path/to/metrics_output.csv -o path/to/gold_standard.json -p 0.4886 --source MIB-Studio-gold
  ```
  Use `-p` to set the pixel-to-micron factor if needed.

### 2. Produce candidate metrics (mib-studio-qt)

- Run **mib-studio-qt**, record an experiment (or use the same input as in step 1 if using playback), and save to HDF5.
- Export metrics from the HDF5 file to gold-standard JSON:
  ```bash
  python scripts/export_hdf5.py -i path/to/experiment.h5 -o path/to/qt_export --format json -p 0.4886
  ```
  This writes `path/to/qt_export/metrics.json` in the same gold-standard format.

### 3. Compare gold vs Qt output

- Run the comparator with the gold-standard file as reference and the Qt export as candidate:
  ```bash
  python scripts/compare_metrics.py path/to/gold_standard.json path/to/qt_export/metrics.json
  ```
- Optionally set per-field tolerances (e.g. for floating-point differences):
  ```bash
  python scripts/compare_metrics.py gold_standard.json qt_export/metrics.json --tolerance deformability 0.001 --tolerance area 0.01 -o report.txt
  ```
- The script prints (or writes with `-o`) a summary: frame counts, matched vs missing, per-field failure counts, and max/mean deltas for numeric fields. Exit code 0 only if all frames match and no differences exceed the tolerances.
