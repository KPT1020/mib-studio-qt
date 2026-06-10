#!/usr/bin/env python3
"""Upload a single file to S3-compatible storage via boto3.

Uses put_object (single PUT) rather than the multipart transfer manager so
that an explicit Content-Length is always sent. Cloudflare strips the header
from the empty-body CreateMultipartUpload POST, causing rustfs to reject with
MissingContentLength; a single PUT with a real body is unaffected.

Botocore 1.42+ auto-appends a CRC32 trailer which forces aws-chunked encoding
and removes Content-Length again. The two env vars below disable that.
payload_signing_enabled forces full-body SigV4 (plain PUT, no trailers).

Credentials come from the standard AWS env vars / --profile /
~/.aws/credentials chain.
"""
from __future__ import annotations

import argparse
import logging
import os
import shlex
import subprocess
import sys

# Disable automatic CRC32 checksum trailers — they force aws-chunked encoding
# which strips Content-Length. Also skip response checksum validation to avoid
# false failures when the server echoes back a checksum we didn't request.
os.environ.setdefault("AWS_REQUEST_CHECKSUM_CALCULATION", "when_required")
os.environ.setdefault("AWS_RESPONSE_CHECKSUM_VALIDATION", "when_required")

def upload_file_to_s3(
    *,
    endpoint: str,
    bucket: str,
    key: str,
    file_path: str,
    content_type: str = "application/octet-stream",
    cache_control: str | None = None,
    acl: str | None = None,
    profile: str | None = None,
    debug: bool = False,
) -> None:
    """Upload a file with explicit Content-Length to S3-compatible storage."""
    try:
        import boto3
        from botocore.config import Config
    except ImportError as exc:
        raise RuntimeError(
            "boto3 is required for S3/R2 uploads. Install it with: python -m pip install boto3"
        ) from exc

    if debug:
        boto3.set_stream_logger("botocore", level=logging.DEBUG)

    session = (
        boto3.Session(profile_name=profile)
        if profile
        else boto3.Session()
    )
    s3 = session.client(
        "s3",
        endpoint_url=endpoint,
        config=Config(
            signature_version="s3v4",
            s3={
                "addressing_style": "path",
                # Full-body SigV4: signs the entire payload upfront and sends
                # a plain PUT with Content-Length — no aws-chunked, no trailers.
                "payload_signing_enabled": True,
            },
        ),
    )

    file_size = os.path.getsize(file_path)
    with open(file_path, "rb") as fh:
        put_args = {
            "Bucket": bucket,
            "Key": key,
            "Body": fh,
            "ContentLength": file_size,
            "ContentType": content_type,
        }
        if cache_control:
            put_args["CacheControl"] = cache_control
        if acl:
            put_args["ACL"] = acl
        s3.put_object(**put_args)


def upload_file_with_wrangler(
    *,
    bucket: str,
    key: str,
    file_path: str,
    content_type: str = "application/octet-stream",
    cache_control: str | None = None,
    wrangler_bin: str = "wrangler",
    remote: bool = True,
) -> None:
    """Upload a file to R2 with Wrangler's authenticated session."""
    cmd = [
        wrangler_bin,
        "r2",
        "object",
        "put",
        f"{bucket}/{key}",
        "--file",
        file_path,
        "--content-type",
        content_type,
        "--force",
    ]
    if cache_control:
        cmd += ["--cache-control", cache_control]
    cmd.append("--remote" if remote else "--local")

    print(f"   Command: {shlex.join(cmd)}")
    subprocess.run(cmd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--bucket", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--file", required=True)
    parser.add_argument("--content-type", default="application/octet-stream")
    parser.add_argument("--cache-control", default=None)
    parser.add_argument("--acl", default=None)
    parser.add_argument("--profile", default=None)
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()

    try:
        upload_file_to_s3(
            endpoint=args.endpoint,
            bucket=args.bucket,
            key=args.key,
            file_path=args.file,
            content_type=args.content_type,
            cache_control=args.cache_control,
            acl=args.acl,
            profile=args.profile,
            debug=args.debug,
        )
    except Exception as e:
        print(f"ERROR: upload failed: {e}", file=sys.stderr)
        return 1

    print(f"uploaded: s3://{args.bucket}/{args.key}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
