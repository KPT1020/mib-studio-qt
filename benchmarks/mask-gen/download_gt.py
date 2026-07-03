"""Fetch the biowork-mask-gen-benchmark ground-truth dataset and build meta.pkl.

The parquet stores file paths (not bytes), so we resolve each image / mask_gt /
mask_pred through the HF LFS endpoint. Requires an HF token with read access to
the (private) dataset in HF_ACCESS_TOKEN (or HF_TOKEN).

    python download_gt.py         # -> cache/*.png, bench.parquet, meta.pkl
"""
import os
import subprocess
import pandas as pd

REPO = "gavinlouuu/biowork-mask-gen-benchmark"
TOKEN = os.environ.get("HF_ACCESS_TOKEN") or os.environ.get("HF_TOKEN")
PARQUET = ("https://huggingface.co/datasets/" + REPO +
           "/resolve/refs%2Fconvert%2Fparquet/default/train/0000.parquet")


def curl(url, out):
    subprocess.run(["curl", "-sSL", "-H", f"Authorization: Bearer {TOKEN}",
                    "-o", out, url], check=True)


def local_name(hf_path):
    rest = hf_path[len("hf://datasets/"):]
    rev, rel = rest.split("@")[1].split("/", 1)
    return rel.replace("/", "__"), rev, rel


def main():
    assert TOKEN, "set HF_ACCESS_TOKEN (read access to the private dataset)"
    os.makedirs("cache", exist_ok=True)
    if not os.path.exists("bench.parquet"):
        curl(PARQUET, "bench.parquet")
    df = pd.read_parquet("bench.parquet")

    rows = []
    for i in range(len(df)):
        r = df.iloc[i]
        rec = dict(idx=i, cls=r["class_name"], conf=float(r["confidence"]),
                   bx0=float(r["bbox_xyxy"][0]), by0=float(r["bbox_xyxy"][1]),
                   bx1=float(r["bbox_xyxy"][2]), by1=float(r["bbox_xyxy"][3]),
                   gt_status=r["gt_status"], needs_review=bool(r["needs_review"]))
        for key, col in [("image", "image"), ("mpred", "mask_pred"), ("mgt", "mask_gt")]:
            fn, rev, rel = local_name(r[col]["path"])
            fp = os.path.join("cache", fn)
            if not os.path.exists(fp) or os.path.getsize(fp) == 0:
                url = f"https://huggingface.co/datasets/{REPO}/resolve/{rev}/{rel}"
                for _ in range(4):
                    curl(url, fp)
                    if os.path.getsize(fp) > 0:
                        break
            rec[key] = fp
        rows.append(rec)

    meta = pd.DataFrame(rows)
    import cv2
    meta["gt_pix"] = [int((cv2.imread(f, cv2.IMREAD_GRAYSCALE) > 0).sum())
                      for f in meta.mgt]
    meta.to_pickle("meta.pkl")
    print(f"meta.pkl: {len(meta)} detections, {meta.image.nunique()} images, "
          f"cache/ has {len(os.listdir('cache'))} files")


if __name__ == "__main__":
    main()
