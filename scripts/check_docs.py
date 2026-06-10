#!/usr/bin/env python3
"""Mechanical checks for the repository knowledge base.

Enforces the harness invariants documented in docs/golden-principles.md:

1. [[WikiLinks]] in knowledge_map/ resolve to real vault notes.
2. Relative markdown links in agent-facing docs resolve to real files.
3. Root AGENTS.md stays a short map (<= 120 lines), not an encyclopedia.
4. Every active execution plan declares a Status: line.

Error messages include remediation instructions so an agent can fix
violations without extra context. Exit code 0 = clean, 1 = violations.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
VAULT = REPO_ROOT / "knowledge_map"
AGENTS_MD_MAX_LINES = 120

WIKILINK_RE = re.compile(r"\[\[([^\]\|#]+)(?:#[^\]\|]*)?(?:\|[^\]]*)?\]\]")
MD_LINK_RE = re.compile(r"(?<!!)\[[^\]]*\]\(([^)\s]+)\)")

# Markdown sources whose relative links must resolve. The vendor pump docs and
# Obsidian config are out of scope.
LINK_CHECK_FILES = [
    REPO_ROOT / "AGENTS.md",
    REPO_ROOT / "CLAUDE.md",
    REPO_ROOT / "README.md",
    *sorted(p for p in (REPO_ROOT / "docs").rglob("*.md") if "Longer Pump" not in str(p)),
]


def vault_notes() -> dict[str, list[Path]]:
    """Map note stem (and vault-relative stem path) to note files."""
    index: dict[str, list[Path]] = {}
    for note in VAULT.rglob("*.md"):
        if ".obsidian" in note.parts:
            continue
        rel = note.relative_to(VAULT)
        index.setdefault(note.stem, []).append(note)
        index.setdefault(str(rel.with_suffix("")), []).append(note)
    return index


def check_wikilinks(errors: list[str]) -> None:
    index = vault_notes()
    for note in sorted(VAULT.rglob("*.md")):
        if ".obsidian" in note.parts:
            continue
        text = note.read_text(encoding="utf-8")
        # Skip fenced code blocks so examples like [[WikiLink]] syntax demos
        # inside ``` blocks are not treated as live links.
        text = re.sub(r"```.*?```", "", text, flags=re.DOTALL)
        for match in WIKILINK_RE.finditer(text):
            target = match.group(1).strip()
            if not target or target == "WikiLinks" or target == "WikiLink":
                continue
            # Relative-path style: [[../services/Foo]] resolved from the note's dir.
            relative_hit = (note.parent / target).with_suffix(".md")
            if target not in index and not relative_hit.exists():
                rel = note.relative_to(REPO_ROOT)
                errors.append(
                    f"{rel}: broken wikilink '[[{target}]]'. Create the note, "
                    f"fix the name, or remove the link. Existing notes: "
                    f"ls knowledge_map/**/*.md"
                )


def check_md_links(errors: list[str]) -> None:
    for md_file in LINK_CHECK_FILES:
        if not md_file.exists():
            continue
        text = md_file.read_text(encoding="utf-8")
        for match in MD_LINK_RE.finditer(text):
            target = match.group(1)
            if target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            path_part = target.split("#", 1)[0]
            if not path_part:
                continue
            resolved = (md_file.parent / path_part).resolve()
            if not resolved.exists():
                rel = md_file.relative_to(REPO_ROOT)
                errors.append(
                    f"{rel}: broken link '{target}'. Fix the path or remove "
                    f"the link; if the target moved, update every reference "
                    f"(rg -l '{path_part}')."
                )


def check_agents_md_length(errors: list[str]) -> None:
    agents_md = REPO_ROOT / "AGENTS.md"
    lines = agents_md.read_text(encoding="utf-8").count("\n") + 1
    if lines > AGENTS_MD_MAX_LINES:
        errors.append(
            f"AGENTS.md is {lines} lines (max {AGENTS_MD_MAX_LINES}). It must "
            f"stay a table of contents: move detail into docs/ or the vault "
            f"and link to it."
        )


def check_active_plans(errors: list[str]) -> None:
    active_dir = REPO_ROOT / "docs" / "exec-plans" / "active"
    for plan in sorted(active_dir.glob("*.md")):
        text = plan.read_text(encoding="utf-8")
        if not re.search(r"^Status:\s*\S", text, flags=re.MULTILINE):
            errors.append(
                f"{plan.relative_to(REPO_ROOT)}: missing 'Status:' line. Add "
                f"'Status: active|blocked|completed' near the top (see "
                f"docs/exec-plans/README.md)."
            )


def main() -> int:
    errors: list[str] = []
    check_wikilinks(errors)
    check_md_links(errors)
    check_agents_md_length(errors)
    check_active_plans(errors)

    if errors:
        print(f"check_docs: {len(errors)} violation(s)\n", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("check_docs: knowledge base OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
