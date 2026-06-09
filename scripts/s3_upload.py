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
import argparse
import logging
import os
import sys

# Disable automatic CRC32 checksum trailers — they force aws-chunked encoding
# which strips Content-Length. Also skip response checksum validation to avoid
# false failures when the server echoes back a checksum we didn't request.
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
    parser.add_argument("--cache-control", default=None)
    parser.add_argument("--acl", default=None)
    parser.add_argument("--profile", default=None)
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()

    if args.debug:
        boto3.set_stream_logger("botocore", level=logging.DEBUG)

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
            s3={
                "addressing_style": "path",
                # Full-body SigV4: signs the entire payload upfront and sends
                # a plain PUT with Content-Length — no aws-chunked, no trailers.
                "payload_signing_enabled": True,
            },
        ),
    )

    file_size = os.path.getsize(args.file)
    try:
        with open(args.file, "rb") as fh:
            put_args = {
                "Bucket": args.bucket,
                "Key": args.key,
                "Body": fh,
                "ContentLength": file_size,
                "ContentType": args.content_type,
            }
            if args.cache_control:
                put_args["CacheControl"] = args.cache_control
            if args.acl:
                put_args["ACL"] = args.acl
            s3.put_object(**put_args)
    except (BotoCoreError, ClientError) as e:
        print(f"ERROR: upload failed: {e}", file=sys.stderr)
        return 1

    print(f"uploaded: s3://{args.bucket}/{args.key}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
