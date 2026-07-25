# Connect a Camera

The **Connect** tab is the first tab and the first stop after launch.
Connecting *selects* a camera — frames only start streaming when you press
**Start Camera** (main tab-bar corner).

![Connect tab with the device list, Refresh/Connect buttons, and mock camera option](images/connect-tab.png)

The **Hardware preflight** checklist at the bottom of the tab reports
camera, processing core, data storage (writability and free space), and
Experiment Profile state as *Passed / Warning / Failed*, each with a
recovery action (`Fix…`). Connecting a camera alone does not complete the
Preflight stage — confirm it in the workflow bar once the checks pass.
Mock cameras are always labeled as simulation so a training session cannot
be mistaken for hardware.

## Automatic connection on startup

A few hundred milliseconds after launch the app enumerates cameras in the
background:

- **Exactly one camera found** — it is selected automatically and the app
  switches to the Overview tab.
- **Multiple cameras** — the device list is populated; pick one and click
  **Connect**.
- **No cameras** — a "No camera found" dialog appears; check cabling and
  the frame-grabber, then click **Refresh**.

After the camera step completes, the app also probes serial ports for the
nanopositioner (status appears in the sidebar).

## Connecting manually

1. Click **Refresh** to re-enumerate devices.
2. Pick a device from the **Cameras**, **MindVision**, or **Framegrabbers**
   groups. The list shows model name, firmware, and interface labels.
3. Click **Connect**. The status line confirms the selection and the app
   switches to Overview with the ROI overlay enabled.

## Mock camera (no hardware)

The mock camera streams images from a folder on disk through the exact same
pipeline as a hardware camera — useful for training, demos, and
reproducing issues away from the instrument.

1. On the Connect tab, click **Configure Mock…**.
2. Pick a folder of frame images (PNG/TIFF/JPEG/BMP; streamed in filename
   order — use numeric prefixes like `frame_0001.png` for deterministic
   playback), the target frame rate, and whether to loop.
3. Click **Start Camera** as usual.

Everything downstream — live view, processing, recording, review — behaves
exactly as with a real camera.
