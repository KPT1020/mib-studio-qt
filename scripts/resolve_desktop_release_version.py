#!/usr/bin/env python3
"""Resolve the next desktop release version without mutating the repository.

The CMake fallback can lag already-published beta/stable tags. Release tooling
must therefore start from the highest release-version line reachable from
``HEAD``, not blindly increment ``DEFAULT_VERSION``.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


VERSION_FILE = Path("cmake/MIBVersion.cmake")
_DEFAULT_VERSION = re.compile(
    r'(?m)^set\(DEFAULT_VERSION\s+"(?P<version>[0-9]+\.[0-9]+\.[0-9]+)"\)\s*$'
)
_RELEASE_TAG = re.compile(
    r"^v?(?P<version>[0-9]+\.[0-9]+\.[0-9]+)"
    r"(?:-beta\.[0-9A-Za-z][0-9A-Za-z.-]*)?$"
)


@dataclass(frozen=True, order=True)
class Version:
    major: int
    minor: int
    patch: int

    @classmethod
    def parse(cls, value: str) -> "Version":
        match = re.fullmatch(r"([0-9]+)\.([0-9]+)\.([0-9]+)", value)
        if match is None:
            raise ValueError(f"Invalid numeric desktop version: {value!r}")
        return cls(*(int(part) for part in match.groups()))

    def __str__(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}"


def release_tag_version(tag: str) -> Version | None:
    match = _RELEASE_TAG.fullmatch(tag.strip())
    return Version.parse(match.group("version")) if match else None


def resolve_current_version(default: Version, reachable_tags: Iterable[str]) -> Version:
    versions = [default]
    versions.extend(
        version
        for tag in reachable_tags
        if (version := release_tag_version(tag)) is not None
    )
    return max(versions)


def bump_version(current: Version, bump: str) -> Version:
    if bump == "none":
        return current
    if bump == "major":
        return Version(current.major + 1, 0, 0)
    if bump == "minor":
        return Version(current.major, current.minor + 1, 0)
    if bump == "patch":
        return Version(current.major, current.minor, current.patch + 1)
    raise ValueError(f"Unsupported bump type: {bump!r}")


def read_default_version(repo_root: Path) -> Version:
    source = (repo_root / VERSION_FILE).read_text(encoding="utf-8")
    matches = list(_DEFAULT_VERSION.finditer(source))
    if len(matches) != 1:
        raise ValueError(
            f"Expected exactly one DEFAULT_VERSION in {VERSION_FILE}, found {len(matches)}"
        )
    return Version.parse(matches[0].group("version"))


def read_reachable_tags(repo_root: Path) -> list[str]:
    result = subprocess.run(
        ["git", "tag", "--merged", "HEAD", "--list"],
        cwd=repo_root,
        check=True,
        capture_output=True,
        text=True,
    )
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def resolve(repo_root: Path, bump: str) -> dict[str, object]:
    default = read_default_version(repo_root)
    tags = read_reachable_tags(repo_root)
    release_tags = [tag for tag in tags if release_tag_version(tag) is not None]
    current = resolve_current_version(default, release_tags)
    prospective = bump_version(current, bump)
    return {
        "default_version": str(default),
        "current_version": str(current),
        "next_version": str(prospective),
        "bump": bump,
        "reachable_release_tags": sorted(release_tags),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        default=str(Path(__file__).resolve().parents[1]),
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--bump",
        choices=("none", "patch", "minor", "major"),
        required=True,
        help="Return the current effective version or the selected prospective bump.",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        payload = resolve(Path(args.repo_root).resolve(), args.bump)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(payload, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
