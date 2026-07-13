# Portable Processing Sync

How a **non-Qt consumer** (e.g. Biowork's `services/mib-processing`) pulls
the same processing config, Young's-modulus LUT, and processing engine the
desktop app uses, so results stay in sync. This is the "publish" side of the
[Biowork portability epic](https://github.com/KPT1020/mib-studio-qt/issues/220);
`bindings/python/` (issue #223) is the engine artifact these manifests point
a consumer at, and `docs/gold_standard_metrics.md` ("Portable Processing
Contract") is the shape the engine's output must match.

All endpoints below are plain HTTPS `GET` on public JSON — no auth, no Qt,
no app dependency. `verify-emodulus-lut-manifest.py` and
`verify-processing-core-manifest.py` demonstrate this with nothing but
Python's stdlib `urllib`.

## Three manifests, one channel

Everything is published per **channel** (`stable` by default) to
`https://updates.yofo.bio/`, via `publish-profiles.py`,
`publish-emodulus-lut.py`, and `publish-processing-core.py`
(`scripts/s3_upload.py` does the actual R2 upload; all three scripts support
`--dry-run` to generate the manifest locally without uploading).

| Manifest | URL | Published by | Consumed for |
|---|---|---|---|
| Profile catalog | `{base}/profiles/{channel}/catalog.json` | `publish-profiles.py` | `ProcessingConfig` values (camera + processing profiles) |
| Emodulus LUT | `{base}/{channel}/emodulus-lut/latest.json` | `publish-emodulus-lut.py` | Young's-modulus lookup table |
| Processing core | `{base}/{channel}/processing-core/latest.json` | `publish-processing-core.py` | Which `mib-processing` wheel + `contract_version` this channel is pinned to |

### Profile catalog (`catalog.json`)

```jsonc
{
  "catalog_schema_version": 1,
  "channel": "stable",
  "published_at": "2026-07-13T00:00:00Z",
  "profiles": [
    {
      "profile_id": "default",
      "display_name": "Default",
      "revision": "2026-07-13T00:00:00Z",
      "profile_meta_url": "...profile.meta.json",
      "config_url": "...config.json",           // ProcessingConfig JSON (see below)
      "camera_script_url": "...egrabberConfig.js", // optional; camera-hardware only, not relevant to a headless consumer
      "config_sha256": "<64-hex>",
      "camera_script_sha256": "<64-hex or null>",
      "app_min_version": "0.8.0",
      "app_max_version": null
    }
  ]
}
```

`config_url` resolves to a JSON document with `config_schema_version: 1` and
the same `image_processing` (nested) structure as
`resources/defaults/config.json`. **Flatten it** before calling
`mib_processing.process_batch`/`compute_processed_frame` — see
`docs/gold_standard_metrics.md` ("ProcessingConfig contract") for the
field-name mapping and the note that the Python bindings' dict shape is
flat, not nested.

### Emodulus LUT manifest (`latest.json`)

```jsonc
{
  "manifest_schema_version": 1,
  "lut_id": "scaled_isoelastic_data_LUT_6.16-4.24",
  "display_name": "Scaled Isoelastic Data Lut 6.16-4.24",
  "revision": "2026.07.13-1",
  "download_url": "...scaled_isoelastic_data_LUT_6.16-4.24.txt",
  "sha256": "<64-hex>",
  "size_bytes": 12345,
  "published_at": "2026-07-13T00:00:00Z",
  "app_min_version": "0.8.0",
  "app_max_version": null
}
```

`download_url` resolves to the tab-separated `area_um  deform  emodulus`
text file `EModulusLut::loadFromFile` / `mib_processing.EModulusLut.load_from_file`
both consume directly.

### Processing core manifest (`processing-core/latest.json`) -- new in A4

The manifest a consumer resolves **first**: it names the pinned combination
of engine version and contract version, and cross-links the other two
manifests so one GET gives a consumer everything it needs to reproduce the
desktop app's results.

```jsonc
{
  "processing_core_manifest_schema_version": 1,
  "channel": "stable",
  "published_at": "2026-07-13T00:00:00Z",
  "contract_version": 1,
  "wheel": {
    "package": "mib-processing",
    "version": "0.1.0",
    "release_tag": "mib-processing-v0.1.0",
    "release_url": "https://github.com/KPT1020/mib-studio-qt/releases/tag/mib-processing-v0.1.0",
    "wheels": [
      {
        "platform_tag": "cp311-cp311-linux_x86_64",
        "url": "https://github.com/KPT1020/mib-studio-qt/releases/download/mib-processing-v0.1.0/mib_processing-0.1.0-cp311-cp311-linux_x86_64.whl",
        "sha256": "<64-hex>"
      }
    ]
  },
  "profile_catalog_url": "https://updates.yofo.bio/profiles/stable/catalog.json",
  "emodulus_lut_manifest_url": "https://updates.yofo.bio/stable/emodulus-lut/latest.json"
}
```

`wheel.wheels[]` is populated from `.github/workflows/python-wheel.yml`'s
release assets for that tag (one entry per Python/platform combination
built). A consumer resolves `pip install <wheels[i].url>`, sha256-verifies
the download, then fetches `profile_catalog_url` /
`emodulus_lut_manifest_url` for the config and LUT pinned to the same
`contract_version`.

**Pin the whole set together.** `contract_version` only changes when the
metrics schema, `ProcessingConfig` schema, or LUT text format changes
incompatibly (see `docs/gold_standard_metrics.md`). A consumer should record
`contract_version` + `wheel.version` + the config/LUT `revision` values it
resolved on every processing run (e.g. as MLflow run params in Biowork's
`services/mib-processing`), so any result is traceable back to the exact
pinned combination that produced it -- not just "some build of the
pipeline."

## Verifying a channel is reachable (no Qt, no app)

```bash
python3 verify-emodulus-lut-manifest.py --manifest-url https://updates.yofo.bio/stable/emodulus-lut/latest.json
python3 verify-processing-core-manifest.py --manifest-url https://updates.yofo.bio/stable/processing-core/latest.json
```

Both use nothing but `urllib`/`json`/`hashlib` from the standard library --
demonstrating that a consumer needs no MIB Studio dependency, Qt or
otherwise, to resolve and validate these manifests.
