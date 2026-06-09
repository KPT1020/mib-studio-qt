#!/usr/bin/env python3
"""Publish a MIB Studio tools zip to Cloudflare R2."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

from scripts.s3_upload import upload_file_to_s3


DEFAULT_BUCKET = "mib-studio-qt-updates"
DEFAULT_PUBLIC_BASE_URL = "https://updates.yofo.bio"
ARTIFACT_CACHE_CONTROL = "public, max-age=31536000, immutable"
MANIFEST_CACHE_CONTROL = "public, max-age=60, must-revalidate"


def join_public_object_url(base_url: str, key: str) -> str:
    return f"{base_url.rstrip('/')}/{key.lstrip('/')}"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def detect_version(zip_path: Path) -> str:
    match = re.fullmatch(r"MIB_Studio_Tools_v(\d+\.\d+\.\d+)_windows\.zip", zip_path.name)
    if not match:
        raise ValueError("Cannot extract version from filename. Expected MIB_Studio_Tools_vX.Y.Z_windows.zip")
    return match.group(1)


def find_default_zip(repo_root: Path) -> Path | None:
    tools_dist = repo_root / "tools" / "dist"
    candidates = sorted(
        tools_dist.glob("MIB_Studio_Tools_v*_windows.zip"),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    return candidates[0] if candidates else None


def write_manifest_file(manifest: dict[str, object], manifest_out: str | None) -> Path:
    if manifest_out:
        path = Path(manifest_out)
        path.parent.mkdir(parents=True, exist_ok=True)
    else:
        handle = tempfile.NamedTemporaryFile(
            "w",
            encoding="utf-8",
            prefix="mib_tools_latest_",
            suffix=".json",
            delete=False,
        )
        path = Path(handle.name)
        handle.close()

    path.write_text(json.dumps(manifest, indent=2, sort_keys=False) + "\n", encoding="utf-8")
    return path


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default=None)
    parser.add_argument("--zip", default=None, help="Path to MIB_Studio_Tools_vX.Y.Z_windows.zip")
    parser.add_argument("--endpoint", default=os.getenv("MIB_STUDIO_R2_ENDPOINT"))
    parser.add_argument("--bucket", default=DEFAULT_BUCKET)
    parser.add_argument("--public-base-url", default=DEFAULT_PUBLIC_BASE_URL)
    parser.add_argument("--channel", default="stable")
    parser.add_argument("--profile", default=os.getenv("MIB_STUDIO_R2_PROFILE"))
    parser.add_argument("--acl", default="")
    parser.add_argument("--manifest-out", default=None, help="Write generated manifest to this path")
    parser.add_argument("--dry-run", action="store_true", help="Generate metadata but do not upload")
    parser.add_argument("--debug", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    repo_root = Path(__file__).resolve().parent
    zip_path = Path(args.zip) if args.zip else find_default_zip(repo_root)

    print("=== Publishing MIB Studio Tools ===")

    if zip_path is None:
        print(
            "ERROR: No tools zip found in tools/dist. Build with tools/build_windows.ps1 then tools/package-tools.ps1",
            file=sys.stderr,
        )
        return 1

    if not zip_path.is_file():
        print(f"ERROR: Zip file does not exist: {zip_path}", file=sys.stderr)
        return 1

    try:
        version = args.version or detect_version(zip_path)
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    if args.version is None:
        print(f"Extracted version from filename: {version}")

    size_bytes = zip_path.stat().st_size
    if size_bytes <= 0:
        print("ERROR: Zip file size is invalid", file=sys.stderr)
        return 1

    print("\n1. Computing SHA-256 hash...")
    digest = sha256_file(zip_path)
    print(f"   Hash: {digest}")
    print(f"   Size: {size_bytes} bytes")

    tools_prefix = f"{args.channel}/tools"
    zip_key = f"{tools_prefix}/{zip_path.name}"
    manifest_key = f"{tools_prefix}/tools-latest.json"
    zip_url = join_public_object_url(args.public_base_url, zip_key)
    manifest_url = join_public_object_url(args.public_base_url, manifest_key)

    print("\n2. Generating manifest...")
    manifest: dict[str, object] = {
        "version": version,
        "zip_url": zip_url,
        "zip_sha256": digest,
        "zip_size_bytes": size_bytes,
        "published_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
    }
    manifest_path = write_manifest_file(manifest, args.manifest_out)
    print(f"   Manifest created: {manifest_path}")

    if args.dry_run:
        print("\nDRY RUN: skipped R2 uploads")
        print("\n=== Publish Preview ===")
        print(f"Manifest URL: {manifest_url}")
        print(f"Zip URL: {zip_url}")
        return 0

    if not args.endpoint:
        if not args.manifest_out:
            manifest_path.unlink(missing_ok=True)
        print(
            "ERROR: R2 S3 API endpoint is required. Set MIB_STUDIO_R2_ENDPOINT or pass --endpoint.",
            file=sys.stderr,
        )
        return 1

    try:
        print("\n3. Uploading zip...")
        upload_file_to_s3(
            endpoint=args.endpoint,
            bucket=args.bucket,
            key=zip_key,
            file_path=str(zip_path),
            content_type="application/zip",
            cache_control=ARTIFACT_CACHE_CONTROL,
            acl=args.acl or None,
            profile=args.profile,
            debug=args.debug,
        )
        print("   Zip uploaded successfully")

        print("\n4. Uploading tools-latest.json...")
        upload_file_to_s3(
            endpoint=args.endpoint,
            bucket=args.bucket,
            key=manifest_key,
            file_path=str(manifest_path),
            content_type="application/json",
            cache_control=MANIFEST_CACHE_CONTROL,
            acl=args.acl or None,
            profile=args.profile,
            debug=args.debug,
        )
        print("   Manifest uploaded successfully")
    except Exception as exc:
        print(f"ERROR: upload failed: {exc}", file=sys.stderr)
        return 1
    finally:
        if not args.manifest_out:
            manifest_path.unlink(missing_ok=True)

    print("\n=== Publish Complete ===")
    print(f"Manifest URL: {manifest_url}")
    print(f"Zip URL: {zip_url}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
