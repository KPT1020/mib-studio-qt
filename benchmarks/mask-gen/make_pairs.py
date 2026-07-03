"""Emit cache/pairs.csv (image,gt,bbox) for the C++ A/B bench.

The C++ benchmark (tests/processing/processing_proposed_pipeline_bench.cpp)
cannot read meta.pkl (a pandas pickle), so this flattens it to a plain CSV of
absolute image / GT paths + GT bbox. Run after download_gt.py.
"""
import os
import pandas as pd


def main():
    meta = pd.read_pickle("meta.pkl")
    n = 0
    with open("cache/pairs.csv", "w") as f:
        f.write("image,gt,bx0,by0,bx1,by1\n")
        for _, r in meta.iterrows():
            if not (os.path.exists(r.image) and os.path.exists(r.mgt)):
                continue
            f.write(f"{os.path.abspath(r.image)},{os.path.abspath(r.mgt)},"
                    f"{int(r.bx0)},{int(r.by0)},{int(r.bx1)},{int(r.by1)}\n")
            n += 1
    print(f"wrote cache/pairs.csv ({n} pairs)")


if __name__ == "__main__":
    main()
