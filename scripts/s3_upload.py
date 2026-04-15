#!/usr/bin/env python3
"""Upload a single file to S3-compatible storage via boto3.

Replaces `aws s3 cp` / `aws s3api put-object` for publish-update.ps1.
boto3's multipart flow always sends Content-Length on
CreateMultipartUpload; aws CLI v2 can omit it and s3.yofo.bio rejects
that with HTTP 400 MissingContentLength.

Credentials come from the standard AWS env vars / --profile /
~/.aws/credentials chain.
"""
import argparse
import os
import sys

# Keep S3-compatible endpoints happy: skip the new CRC32 trailers that
# some servers (rustfs, older MinIO, etc.) reject.
os.environ.setdefault("AWS_REQUEST_CHECKSUM_CALCULATION", "when_required")
os.environ.setdefault("AWS_RESPONSE_CHECKSUM_VALIDATION", "when_required")

import boto3
from botocore.config import Config
from botocore.exceptions import BotoCoreError, ClientError


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--bucket", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--file", required=True)
    parser.add_argument("--content-type", default="application/octet-stream")
    parser.add_argument("--acl", default="public-read")
    parser.add_argument("--profile", default=None)
    args = parser.parse_args()

    session = (
        boto3.Session(profile_name=args.profile)
        if args.profile
        else boto3.Session()
    )
    s3 = session.client(
        "s3",
        endpoint_url=args.endpoint,
        config=Config(
            signature_version="s3v4",
            s3={"addressing_style": "path"},
        ),
    )

    try:
        s3.upload_file(
            args.file,
            args.bucket,
            args.key,
            ExtraArgs={
                "ContentType": args.content_type,
                "ACL": args.acl,
            },
        )
    except (BotoCoreError, ClientError) as e:
        print(f"ERROR: upload failed: {e}", file=sys.stderr)
        return 1

    print(f"uploaded: s3://{args.bucket}/{args.key}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
