#!/usr/bin/env python3
"""Publish a MIB Studio app update package to Cloudflare R2."""
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

from scripts.s3_upload import upload_file_to_s3, upload_file_with_wrangler


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


def detect_version(installer: Path) -> str:
    match = re.fullmatch(r"MIB_Studio_Qt_(?:Setup|Update)_v(\d+\.\d+\.\d+)\.exe", installer.name)
    if not match:
        raise ValueError(
            "Cannot extract version from filename. Expected "
            "MIB_Studio_Qt_Setup_v<version>.exe or MIB_Studio_Qt_Update_v<version>.exe"
        )
    return match.group(1)


def write_manifest_file(manifest: dict[str, object], manifest_out: str | None) -> Path:
    if manifest_out:
        path = Path(manifest_out)
        path.parent.mkdir(parents=True, exist_ok=True)
    else:
        handle = tempfile.NamedTemporaryFile(
            "w",
            encoding="utf-8",
            prefix="mib_studio_qt_latest_",
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
    parser.add_argument("--installer", required=True, help="Path to MIB_Studio_Qt_(Update|Setup)_vX.Y.Z.exe")
    parser.add_argument("--endpoint", default=os.getenv("MIB_STUDIO_R2_ENDPOINT"))
    parser.add_argument("--bucket", default=DEFAULT_BUCKET)
    parser.add_argument("--public-base-url", default=DEFAULT_PUBLIC_BASE_URL)
    parser.add_argument("--channel", default="stable")
    parser.add_argument("--profile", default=os.getenv("MIB_STUDIO_R2_PROFILE"))
    parser.add_argument("--acl", default="")
    parser.add_argument("--release-notes-url", default="")
    parser.add_argument("--manifest-out", default=None, help="Write generated manifest to this path")
    parser.add_argument("--dry-run", action="store_true", help="Generate metadata but do not upload")
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


def upload_object(
    *,
    args: argparse.Namespace,
    key: str,
    file_path: Path,
    content_type: str,
    cache_control: str,
) -> None:
    method = resolve_upload_method(args.upload_method, args.endpoint)
    if method == "s3":
        if not args.endpoint:
            raise RuntimeError("R2 S3 API endpoint is required for --upload-method s3")
        upload_file_to_s3(
            endpoint=args.endpoint,
            bucket=args.bucket,
            key=key,
            file_path=str(file_path),
            content_type=content_type,
            cache_control=cache_control,
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
        content_type=content_type,
        cache_control=cache_control,
        wrangler_bin=args.wrangler_bin,
    )


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    installer = Path(args.installer)

    print("=== Publishing MIB Studio Qt Update ===")

    if not installer.is_file():
        print(f"ERROR: Installer file does not exist: {installer}", file=sys.stderr)
        return 1

    try:
        version = args.version or detect_version(installer)
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    if args.version is None:
        print(f"Extracted version from filename: {version}")

    package_kind = "update package (app files only)" if "MIB_Studio_Qt_Update_v" in installer.name else "full installer"
    print(f"Detected {package_kind}")

    size_bytes = installer.stat().st_size
    if size_bytes <= 0:
        print(f"ERROR: Installer file size is invalid: {size_bytes}", file=sys.stderr)
        return 1

    print("\n1. Computing SHA-256 hash...")
    digest = sha256_file(installer)
    print(f"   Hash: {digest}")
    print(f"   Size: {size_bytes} bytes")

    installer_key = f"{args.channel}/{installer.name}"
    manifest_key = f"{args.channel}/latest.json"
    installer_url = join_public_object_url(args.public_base_url, installer_key)
    manifest_url = join_public_object_url(args.public_base_url, manifest_key)

    print("\n2. Generating manifest...")
    manifest: dict[str, object] = {
        "version": version,
        "installer_url": installer_url,
        "installer_sha256": digest,
        "installer_size_bytes": size_bytes,
        "published_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
    }
    if args.release_notes_url:
        manifest["release_notes_url"] = args.release_notes_url

    manifest_path = write_manifest_file(manifest, args.manifest_out)
    print(f"   Manifest created: {manifest_path}")

    if args.dry_run:
        print("\nDRY RUN: skipped R2 uploads")
        print("\n=== Publish Preview ===")
        print(f"Manifest URL: {manifest_url}")
        print(f"Installer URL: {installer_url}")
        return 0

    try:
        print("\n3. Uploading installer...")
        upload_object(
            args=args,
            key=installer_key,
            file_path=installer,
            content_type="application/x-msdownload",
            cache_control=ARTIFACT_CACHE_CONTROL,
        )
        print("   Installer uploaded successfully")

        print("\n4. Uploading manifest...")
        upload_object(
            args=args,
            key=manifest_key,
            file_path=manifest_path,
            content_type="application/json",
            cache_control=MANIFEST_CACHE_CONTROL,
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
    print(f"Installer URL: {installer_url}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
