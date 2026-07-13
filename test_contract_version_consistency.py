#!/usr/bin/env python3
"""Guards against contract_version drift across its independent declarations.

contract_version (docs/gold_standard_metrics.md, "Portable Processing
Contract") is currently hardcoded in six places rather than read from one
source of truth -- a real drift risk flagged when publish-processing-core.py
(A4) was added. This test is the cheap safety net until/unless that's
refactored into a single source: it fails loudly if any declaration is
bumped without the others, rather than letting them silently diverge.
"""

from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent


class ContractVersionConsistencyTest(unittest.TestCase):
    def test_all_declarations_agree(self) -> None:
        declarations: dict[str, int] = {}

        export_hdf5 = (REPO_ROOT / "scripts" / "export_hdf5.py").read_text(encoding="utf-8")
        match = re.search(r"GOLD_STANDARD_SCHEMA_VERSION\s*=\s*(\d+)", export_hdf5)
        self.assertIsNotNone(match, "GOLD_STANDARD_SCHEMA_VERSION not found in scripts/export_hdf5.py")
        declarations["scripts/export_hdf5.py"] = int(match.group(1))

        convert_csv = (REPO_ROOT / "scripts" / "convert_legacy_csv_to_json.py").read_text(encoding="utf-8")
        match = re.search(r"GOLD_STANDARD_SCHEMA_VERSION\s*=\s*(\d+)", convert_csv)
        self.assertIsNotNone(match, "GOLD_STANDARD_SCHEMA_VERSION not found in scripts/convert_legacy_csv_to_json.py")
        declarations["scripts/convert_legacy_csv_to_json.py"] = int(match.group(1))

        schema = json.loads((REPO_ROOT / "docs" / "gold_standard_metrics.schema.json").read_text(encoding="utf-8"))
        declarations["docs/gold_standard_metrics.schema.json"] = schema["properties"]["version"]["const"]

        bindings_cpp = (REPO_ROOT / "bindings" / "python" / "src" / "bindings.cpp").read_text(encoding="utf-8")
        match = re.search(r'm\.attr\("CONTRACT_VERSION"\)\s*=\s*(\d+)', bindings_cpp)
        self.assertIsNotNone(match, "CONTRACT_VERSION not found in bindings/python/src/bindings.cpp")
        declarations["bindings/python/src/bindings.cpp"] = int(match.group(1))

        publish_script = (REPO_ROOT / "publish-processing-core.py").read_text(encoding="utf-8")
        match = re.search(r"DEFAULT_CONTRACT_VERSION\s*=\s*(\d+)", publish_script)
        self.assertIsNotNone(match, "DEFAULT_CONTRACT_VERSION not found in publish-processing-core.py")
        declarations["publish-processing-core.py"] = int(match.group(1))

        config = json.loads((REPO_ROOT / "resources" / "defaults" / "config.json").read_text(encoding="utf-8"))
        declarations["resources/defaults/config.json"] = config["config_schema_version"]

        values = set(declarations.values())
        self.assertEqual(
            len(values),
            1,
            f"contract_version declarations disagree: {declarations}. "
            "Bump every declaration together, or fold them into one source of truth.",
        )


if __name__ == "__main__":
    unittest.main()
