# MIB Studio Tools

Standalone end-user tools for working with MIB Studio HDF5 files after recording. They are distributed as **separate downloads** (no Python required) and are versioned to match the main app.

## What is included

| Tool | Type | Description |
|------|------|-------------|
| **HDF5 Export** | GUI | Export metrics and images from an .h5 file (CSV, TIFF). Double-click or run from command line. |
| **Reanalyse HDF5** | CLI | Re-run the processing pipeline on an .h5 and save intermediate images (blur, diff, threshold, mask) and optional metrics CSV / reanalysis.h5. |

## Download

- **Windows:** Versioned zip per release, e.g. `MIB_Studio_Tools_vX.Y.Z_windows.zip`, containing `hdf5_export_app.exe`, `mib_reanalyse_hdf5.exe`, and `README.txt`.
- **macOS:** The `macOS HDF5 Exporter` workflow produces separate native DMGs
  for Apple Silicon (`arm64`) and Intel (`x86_64`). You can also build from
  source with `./tools/build_mac.sh --clean --dmg --exporter-only`; see
  [tools/README.md](../../tools/README.md).

Published builds are uploaded to the same Cloudflare R2 update bucket as the main app, under the `stable/tools/` prefix. A manifest `tools-latest.json` is updated so the latest tools zip URL can be fetched programmatically.

Production URLs:

- Manifest: `https://updates.yofo.bio/stable/tools/tools-latest.json`
- Zip: `https://updates.yofo.bio/stable/tools/MIB_Studio_Tools_vX.Y.Z_windows.zip`

## Quick start (Windows, from the zip)

1. Unzip `MIB_Studio_Tools_vX.Y.Z_windows.zip`.
2. **HDF5 Export:** Run `hdf5_export_app.exe`, choose input .h5 and output directory, then Export.
3. **Reanalyse HDF5:** Open a command prompt in the unzipped folder and run:
   ```text
   mib_reanalyse_hdf5.exe -i path\to\experiment.h5 -o path\to\reanalysis
   mib_reanalyse_hdf5.exe --help
   ```

## Quick start (macOS, from the DMG)

1. Download the DMG matching the Mac: `arm64` for Apple Silicon (M1 or newer),
   or `x86_64` for Intel.
2. Open the DMG and drag `hdf5_export_app.app` to Applications.
3. These internal builds are ad-hoc signed but not Apple notarized. On first
   launch, Control-click the app, choose **Open**, then confirm **Open**.
4. Choose the input `.h5` file and output directory, then select **Export**.

## Compatibility

Tools version **X.Y.Z** are intended for use with **MIB Studio Qt vX.Y.Z** and the HDF5 schema produced by that app version. Use the tools zip that matches your app version when possible.

## More information

- **HDF5 Export (GUI):** [hdf5-export-app.md](hdf5-export-app.md) — build from source, options, troubleshooting.
- **Reanalyse HDF5 (CLI):** [reanalyse-hdf5.md](reanalyse-hdf5.md) — options, output layout, parameters.
- **Classification:** [supported-tools-classification.md](../supported-tools-classification.md) — which scripts are supported end-user tools vs dev-only.
