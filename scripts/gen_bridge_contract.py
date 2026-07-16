#!/usr/bin/env python3
"""Generate (or verify) the TypeScript bridge-contract mirror.

Source of truth: crates/mib-bridge/contract/bridge-contract.json (BE-1,
issue #271, ADR 0004). This script renders it to desktop/src/bridgeContract.ts
so the TypeScript layer cannot drift silently from the C++/Rust contract.

Usage:
    python3 scripts/gen_bridge_contract.py            # (re)write the TS file
    python3 scripts/gen_bridge_contract.py --check    # CI drift gate: exit 1
                                                      # if the TS file differs

The C++ side is pinned by static_asserts in crates/mib-bridge/src/shim.cpp and
the Rust side by crates/mib-bridge/tests/contract.rs — both against the same
JSON values.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CONTRACT_JSON = REPO_ROOT / "crates/mib-bridge/contract/bridge-contract.json"
OUTPUT_TS = REPO_ROOT / "desktop/src/bridgeContract.ts"

ENUM_GROUPS = [
    ("event_kinds", "EVENT_KINDS"),
    ("command_types", "COMMAND_TYPES"),
    ("camera_types", "CAMERA_TYPES"),
    ("camera_selection_modes", "CAMERA_SELECTION_MODES"),
    ("experiment_states", "EXPERIMENT_STATES"),
    ("error_sources", "ERROR_SOURCES"),
    ("operation_kinds", "OPERATION_KINDS"),
    ("operation_states", "OPERATION_STATES"),
    ("camera_states", "CAMERA_STATES"),
    ("recording_states", "RECORDING_STATES"),
]


def render(contract: dict) -> str:
    lines = [
        "// GENERATED FILE — do not edit by hand.",
        "// Source of truth: crates/mib-bridge/contract/bridge-contract.json",
        "// Regenerate with: python3 scripts/gen_bridge_contract.py",
        "// CI verifies this file with: python3 scripts/gen_bridge_contract.py --check",
        "",
        f"export const BRIDGE_ABI_VERSION = {contract['abi_version']};",
        "",
    ]
    for json_key, ts_name in ENUM_GROUPS:
        entries = contract[json_key]
        lines.append(f"export const {ts_name} = {{")
        for name, value in entries.items():
            lines.append(f"  {name}: {value},")
        lines.append("} as const;")
        lines.append("")
    # Reverse lookup for event kinds: numeric value -> stable name.
    lines.append("export const EVENT_KIND_NAMES: Readonly<Record<number, string>> = {")
    for name, value in contract["event_kinds"].items():
        lines.append(f"  {value}: \"{name}\",")
    lines.append("};")
    lines.append("")
    lines.append("export type BridgeEventKindName = keyof typeof EVENT_KINDS;")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    check = "--check" in sys.argv[1:]
    contract = json.loads(CONTRACT_JSON.read_text(encoding="utf-8"))

    # Structural sanity: values inside a group must be unique and contiguous
    # from 0 (the contract is additive-append-only).
    for json_key, _ in ENUM_GROUPS:
        values = sorted(contract[json_key].values())
        if values != list(range(len(values))):
            print(
                f"gen_bridge_contract: {json_key} values must be contiguous "
                f"from 0 (got {values}) — the contract is append-only",
                file=sys.stderr,
            )
            return 1

    rendered = render(contract)
    if check:
        current = OUTPUT_TS.read_text(encoding="utf-8") if OUTPUT_TS.exists() else ""
        if current != rendered:
            print(
                "gen_bridge_contract: drift detected — desktop/src/bridgeContract.ts "
                "does not match crates/mib-bridge/contract/bridge-contract.json.\n"
                "Run: python3 scripts/gen_bridge_contract.py",
                file=sys.stderr,
            )
            return 1
        print("gen_bridge_contract: TypeScript contract mirror is in sync")
        return 0

    OUTPUT_TS.write_text(rendered, encoding="utf-8")
    print(f"gen_bridge_contract: wrote {OUTPUT_TS.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
