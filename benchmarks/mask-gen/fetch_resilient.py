"""Resilient GT fetch: retries each file, tolerates transient resets, resumes.
Builds meta.pkl identical in schema to download_gt.py."""
import os, subprocess, time
import pandas as pd

REPO = "gavinlouuu/biowork-mask-gen-benchmark"
TOKEN = os.environ.get("HF_ACCESS_TOKEN") or os.environ.get("HF_TOKEN")


def curl(url, out):
    for attempt in range(8):
        r = subprocess.run(["curl", "-sSL", "--retry", "3", "--retry-delay", "1",
                            "-H", f"Authorization: Bearer {TOKEN}", "-o", out, url])
        if r.returncode == 0 and os.path.exists(out) and os.path.getsize(out) > 0:
            return True
        time.sleep(min(2 ** attempt, 20))
    return False


def local_name(hf_path):
    rest = hf_path[len("hf://datasets/"):]
    rev, rel = rest.split("@")[1].split("/", 1)
    return rel.replace("/", "__"), rev, rel


def main():
    assert TOKEN, "set HF_ACCESS_TOKEN"
    os.makedirs("cache", exist_ok=True)
    df = pd.read_parquet("bench.parquet")
    rows, missing = [], 0
    for i in range(len(df)):
        r = df.iloc[i]
        rec = dict(idx=i, cls=r["class_name"], conf=float(r["confidence"]),
                   bx0=float(r["bbox_xyxy"][0]), by0=float(r["bbox_xyxy"][1]),
                   bx1=float(r["bbox_xyxy"][2]), by1=float(r["bbox_xyxy"][3]),
                   gt_status=r["gt_status"], needs_review=bool(r["needs_review"]))
        ok = True
        for key, col in [("image", "image"), ("mpred", "mask_pred"), ("mgt", "mask_gt")]:
            fn, rev, rel = local_name(r[col]["path"])
            fp = os.path.join("cache", fn)
            if not os.path.exists(fp) or os.path.getsize(fp) == 0:
                url = f"https://huggingface.co/datasets/{REPO}/resolve/{rev}/{rel}"
                if not curl(url, fp):
                    ok = False
                    missing += 1
                    print(f"  MISSING idx={i} {key}")
            rec[key] = fp
        if ok:
            rows.append(rec)
    meta = pd.DataFrame(rows)
    import cv2
    meta["gt_pix"] = [int((cv2.imread(f, cv2.IMREAD_GRAYSCALE) > 0).sum()) for f in meta.mgt]
    meta.to_pickle("meta.pkl")
    print(f"meta.pkl: {len(meta)} detections OK, {missing} files missing, "
          f"{meta.image.nunique()} images, cache/ has {len(os.listdir('cache'))} files")


if __name__ == "__main__":
    main()
