"""Fetch the full 512x96 stream (gavinlouuu/512x96stream, 5000 frames) and
match the benchmark GT frames to their stream positions by exact pixel hash.

Produces:
  stream_frames.npy   (5000, 96, 512) uint8   -- gitignored (~245 MB)
  gt_stream_idx.json  {gt_image_path: stream_index}

Requires meta.pkl (run download_gt.py first) and HF_ACCESS_TOKEN.
"""
import os, io, json, hashlib, subprocess
import numpy as np, pandas as pd, cv2
from PIL import Image

REPO = "gavinlouuu/512x96stream"
TOKEN = os.environ.get("HF_ACCESS_TOKEN") or os.environ.get("HF_TOKEN")
PARQUET = ("https://huggingface.co/datasets/" + REPO +
           "/resolve/refs%2Fconvert%2Fparquet/default/train/0000.parquet")


def main():
    assert TOKEN, "set HF_ACCESS_TOKEN"
    if not os.path.exists("stream.parquet"):
        subprocess.run(["curl", "-sSL", "-H", f"Authorization: Bearer {TOKEN}",
                        "-o", "stream.parquet", PARQUET], check=True)
    import pyarrow.parquet as pq
    col = pq.read_table("stream.parquet").column("image").to_pylist()

    def dec(c):
        b = c["bytes"] if isinstance(c, dict) else c
        return np.array(Image.open(io.BytesIO(b)).convert("L"))

    frames = np.stack([dec(c) for c in col])
    np.save("stream_frames.npy", frames)
    H = {hashlib.md5(frames[i].tobytes()).hexdigest(): i for i in range(len(frames))}

    meta = pd.read_pickle("meta.pkl")
    idx = {}
    for f in meta.image.unique():
        h = hashlib.md5(cv2.imread(f, cv2.IMREAD_GRAYSCALE).tobytes()).hexdigest()
        if h in H:
            idx[f] = H[h]
    json.dump(idx, open("gt_stream_idx.json", "w"))
    print(f"stream frames: {len(frames)}  GT matched to stream: {len(idx)}/{meta.image.nunique()}")
    if idx:
        si = sorted(idx.values())
        print(f"matched stream index range: {si[0]}..{si[-1]}")


if __name__ == "__main__":
    main()
