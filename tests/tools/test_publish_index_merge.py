"""Unit tests for publish-update.py's merge_index (per-channel version index)."""
import importlib.util
import pathlib
import sys

_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_ROOT))  # so publish-update.py can import scripts.s3_upload
_spec = importlib.util.spec_from_file_location("pub", _ROOT / "publish-update.py")
pub = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pub)


def entry(v):
    return {
        "version": v,
        "installer_url": f"https://updates.yofo.bio/stable/u_{v}.exe",
        "installer_sha256": "s",
        "installer_size_bytes": 1,
        "release_notes_url": "n",
        "published_utc": "t",
    }


def test_insert_into_empty():
    out = pub.merge_index({}, entry("1.0.4"), channel="stable")
    assert out["schema_version"] == 1
    assert out["channel"] == "stable"
    assert [v["version"] for v in out["versions"]] == ["1.0.4"]


def test_dedupe_and_order():
    idx = pub.merge_index({}, entry("1.0.3"), channel="stable")
    idx = pub.merge_index(idx, entry("1.0.4"), channel="stable")
    idx = pub.merge_index(idx, entry("1.0.3"), channel="stable")  # duplicate version
    versions = [v["version"] for v in idx["versions"]]
    assert versions == ["1.0.4", "1.0.3"], versions


def test_beta_orders_below_its_release():
    idx = pub.merge_index({}, entry("1.0.4-beta.1"), channel="beta")
    idx = pub.merge_index(idx, entry("1.0.4-beta.2"), channel="beta")
    idx = pub.merge_index(idx, entry("1.0.4"), channel="beta")
    versions = [v["version"] for v in idx["versions"]]
    assert versions == ["1.0.4", "1.0.4-beta.2", "1.0.4-beta.1"], versions


def test_replace_keeps_latest_fields():
    idx = pub.merge_index({}, entry("1.0.4"), channel="stable")
    updated = entry("1.0.4")
    updated["installer_sha256"] = "newhash"
    idx = pub.merge_index(idx, updated, channel="stable")
    assert len(idx["versions"]) == 1
    assert idx["versions"][0]["installer_sha256"] == "newhash"


if __name__ == "__main__":
    test_insert_into_empty()
    test_dedupe_and_order()
    test_beta_orders_below_its_release()
    test_replace_keeps_latest_fields()
    print("ok")
