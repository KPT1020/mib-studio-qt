#!/usr/bin/env python3
"""Publish the processing-core manifest (pinned engine + contract version) to Cloudflare R2.

See docs/portable-processing-sync.md for the manifest schema and how a
non-Qt consumer (e.g. Biowork's services/mib-processing) resolves it
together with the profile catalog and emodulus LUT manifest.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from scripts.s3_upload import upload_file_to_s3, upload_file_with_wrangler

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - Python <3.11 fallback
    tomllib = None  # type: ignore[assignment]


DEFAULT_BUCKET = "mib-studio-qt-updates"
DEFAULT_PUBLIC_BASE_URL = "https://updates.yofo.bio"
DEFAULT_REPO = "KPT1020/mib-studio-qt"
MANIFEST_SCHEMA_VERSION = 1
DEFAULT_CONTRACT_VERSION = 1
MANIFEST_CACHE_CONTROL = "public, max-age=60, must-revalidate"


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def join_public_object_url(base_url: str, key: str) -> str:
    return f"{base_url.rstrip('/')}/{key.lstrip('/')}"


def read_wheel_version(pyproject_path: Path) -> str:
    if tomllib is None:
        raise RuntimeError(
            "Reading the wheel version from pyproject.toml requires Python 3.11+ "
            "(tomllib); pass --wheel-version explicitly instead."
        )
    with pyproject_path.open("rb") as fh:
        data = tomllib.load(fh)
    try:
        return str(data["project"]["version"])
    except KeyError as exc:
        raise ValueError(f"{pyproject_path} has no [project].version") from exc


def build_wheel_entries(wheel_paths: list[Path], repo: str, release_tag: str) -> list[dict[str, Any]]:
    entries = []
    for path in wheel_paths:
        if not path.is_file():
            raise ValueError(f"Wheel file does not exist: {path}")
        # Wheel filename: {package}-{version}-{python_tag}-{abi_tag}-{platform_tag}.whl
        stem = path.stem
        parts = stem.split("-")
        platform_tag = "-".join(parts[2:]) if len(parts) >= 5 else stem
        entries.append({
            "platform_tag": platform_tag,
            "url": f"https://github.com/{repo}/releases/download/{release_tag}/{path.name}",
            "sha256": sha256_file(path),
        })
    return entries


def build_manifest(
    *,
    channel: str,
    contract_version: int,
    wheel_version: str,
    release_tag: str,
    repo: str,
    wheel_paths: list[Path],
    public_base_url: str,
) -> dict[str, Any]:
    return {
        "processing_core_manifest_schema_version": MANIFEST_SCHEMA_VERSION,
        "channel": channel,
        "published_at": utc_now(),
        "contract_version": contract_version,
        "wheel": {
            "package": "mib-processing",
            "version": wheel_version,
            "release_tag": release_tag,
            "release_url": f"https://github.com/{repo}/releases/tag/{release_tag}",
            "wheels": build_wheel_entries(wheel_paths, repo, release_tag),
        },
        "profile_catalog_url": join_public_object_url(public_base_url, f"profiles/{channel}/catalog.json"),
        "emodulus_lut_manifest_url": join_public_object_url(public_base_url, f"{channel}/emodulus-lut/latest.json"),
    }


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--channel", default="stable")
    parser.add_argument("--contract-version", type=int, default=DEFAULT_CONTRACT_VERSION)
    parser.add_argument(
        "--wheel-version",
        default=None,
        help="Wheel version to publish. Defaults to bindings/python/pyproject.toml's [project].version.",
    )
    parser.add_argument(
        "--pyproject",
        default=str(Path(__file__).resolve().parent / "bindings" / "python" / "pyproject.toml"),
        help="Path to pyproject.toml to read the default wheel version from.",
    )
    parser.add_argument("--release-tag", default=None, help="Defaults to mib-processing-v<wheel-version>")
    parser.add_argument("--repo", default=DEFAULT_REPO, help="owner/repo for GitHub Release URLs")
    parser.add_argument(
        "--wheel",
        action="append",
        default=[],
        dest="wheels",
        help="Path to a built .whl file to list in the manifest; repeat for multiple platforms/Python versions.",
    )
    parser.add_argument("--endpoint", default=os.getenv("MIB_STUDIO_R2_ENDPOINT"))
    parser.add_argument("--bucket", default=DEFAULT_BUCKET)
    parser.add_argument("--public-base-url", default=DEFAULT_PUBLIC_BASE_URL)
    parser.add_argument("--profile", default=os.getenv("MIB_STUDIO_R2_PROFILE"))
    parser.add_argument("--acl", default="")
    parser.add_argument("--manifest-out", default=None, help="Write generated manifest to this path")
    parser.add_argument("--dry-run", action="store_true", help="Generate the manifest but do not upload")
    parser.add_argument(
        "--upload-method",
        choices=("auto", "s3", "wrangler"),
        default="auto",
        help="Upload through S3 credentials or the authenticated Wrangler CLI",
    )
    parser.add_argument("--wrangler-bin", default=os.getenv("WRANGLER_BIN", "wrangler"))
    parser.add_argument("--debug", action="store_true")
    return parser


def resolve_upload_method(upload_method: str, endpoint: str | None) -> str:
    if upload_method == "auto":
        return "s3" if endpoint else "wrangler"
    return upload_method


def upload_manifest(*, args: argparse.Namespace, key: str, file_path: Path) -> None:
    method = resolve_upload_method(args.upload_method, args.endpoint)
    if method == "s3":
        if not args.endpoint:
            raise RuntimeError("R2 S3 API endpoint is required for --upload-method s3")
        upload_file_to_s3(
            endpoint=args.endpoint,
            bucket=args.bucket,
            key=key,
            file_path=str(file_path),
            content_type="application/json",
            cache_control=MANIFEST_CACHE_CONTROL,
            acl=args.acl or None,
            profile=args.profile,
            debug=args.debug,
        )
        return

    if args.acl:
        print("   Note: --acl is ignored for Wrangler uploads; R2 public access is configured on the bucket/domain.")
    upload_file_with_wrangler(
        bucket=args.bucket,
        key=key,
        file_path=str(file_path),
        content_type="application/json",
        cache_control=MANIFEST_CACHE_CONTROL,
        wrangler_bin=args.wrangler_bin,
    )


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    print("=== Publishing Processing Core Manifest ===")

    try:
        wheel_version = args.wheel_version or read_wheel_version(Path(args.pyproject))
    except (RuntimeError, ValueError, FileNotFoundError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    release_tag = args.release_tag or f"mib-processing-v{wheel_version}"
    wheel_paths = [Path(p) for p in args.wheels]

    try:
        manifest = build_manifest(
            channel=args.channel,
            contract_version=args.contract_version,
            wheel_version=wheel_version,
            release_tag=release_tag,
            repo=args.repo,
            wheel_paths=wheel_paths,
            public_base_url=args.public_base_url,
        )
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    manifest_key = f"{args.channel}/processing-core/latest.json"
    manifest_url = join_public_object_url(args.public_base_url, manifest_key)

    if args.manifest_out:
        manifest_path = Path(args.manifest_out)
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
    else:
        handle = tempfile.NamedTemporaryFile(
            "w", encoding="utf-8", prefix="processing_core_manifest_", suffix=".json", delete=False,
        )
        manifest_path = Path(handle.name)
        handle.close()
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=False) + "\n", encoding="utf-8")

    print(f"Channel: {args.channel}")
    print(f"Contract version: {args.contract_version}")
    print(f"Wheel version: {wheel_version} ({len(wheel_paths)} platform wheel(s))")
    print(f"Manifest: {manifest_path}")
    print(f"Manifest URL: {manifest_url}")

    if args.dry_run:
        print("\nDRY RUN: skipped R2 upload")
        print(f"Would upload: {manifest_path} -> s3://{args.bucket}/{manifest_key}")
        return 0

    try:
        print(f"\nUploading {manifest_key}...")
        upload_manifest(args=args, key=manifest_key, file_path=manifest_path)
    except Exception as exc:
        print(f"ERROR: publish failed: {exc}", file=sys.stderr)
        return 1
    finally:
        if not args.manifest_out:
            manifest_path.unlink(missing_ok=True)

    print("\n=== Publish Complete ===")
    print(f"Manifest URL: {manifest_url}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
