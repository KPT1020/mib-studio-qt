# Models

## Convert `yolo11n-seg.pt` to `yolo11n-seg.onnx`

From the repo root (PowerShell):

```powershell
python -m venv .venv
.\.venv\Scripts\python -m pip install -U pip
.\.venv\Scripts\python -m pip install ultralytics onnx
.\.venv\Scripts\python .\resources\models\convert_yolo11n_seg_to_onnx.py
```

Optional flags:

```powershell
.\.venv\Scripts\python .\resources\models\convert_yolo11n_seg_to_onnx.py --dynamic
.\.venv\Scripts\python .\resources\models\convert_yolo11n_seg_to_onnx.py --simplify
```

Expected output:
- `resources/models/yolo11n-seg.onnx`

