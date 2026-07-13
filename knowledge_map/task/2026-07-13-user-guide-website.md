# User-guide website from the generated workflow screenshots

**Date:** 2026-07-13 · **Issue:** #233 · **Status:** shipped

## What

Published `docs/manual/` as a searchable static website (intro + user
guide) at <https://kpt1020.github.io/mib-studio-qt/>, built from the
auto-generated `screenshot_tour` screenshots so operators have a place to
navigate when in doubt.

## How

- **`mkdocs.yml`** (repo root) — MkDocs Material config with
  `docs_dir: docs/manual`. Renders the existing manual pages as-is (no
  duplication); nav mirrors the workflow order in `docs/manual/README.md`.
  `README.md` is excluded from the site (it stays the in-repo index with
  regeneration instructions); the new **`docs/manual/index.md`** is the
  site landing page — a visual tour built from the eight registry
  screenshots, plus the "generated, not hand-captured" policy statement.
- **`.github/workflows/docs-site.yml`** — builds with
  `mkdocs build --strict` (PRs build-only) and deploys to GitHub Pages
  via `actions/upload-pages-artifact` + `actions/deploy-pages` on pushes
  to `main` touching `docs/manual/**` or `mkdocs.yml`. Because
  `build-windows.yml` (release mode) commits refreshed screenshots back
  to `main`, that same push re-triggers this workflow — release-time
  screenshot regeneration flows through to the published site with no
  extra wiring.
- `docs/manual/review-and-postprocess.md`'s one link outside the manual
  (`../howto/tools.md`) now points at the GitHub blob URL so the site
  build has no out-of-tree relative links and `--strict` stays clean.

## Gotchas

- GitHub Pages must be set to **Source: GitHub Actions** in the repo
  settings once, or the deploy job fails.
- `scripts/check_screenshots.py` scans **all** `docs/manual/*.md` for
  `images/*.png` references — any image embedded in `index.md` must be a
  shot id in the `screenshot_tour` registry
  (`src/frontend/tools/screenshot_tour_main.cpp`).
- `mkdocs build --strict` fails on any broken intra-site link; keep manual
  cross-links inside `docs/manual/` (or use absolute GitHub URLs).

## Related

[[../frontend/Screenshot-Tour]] · `2026-07-13-user-manual-screenshots.md`
