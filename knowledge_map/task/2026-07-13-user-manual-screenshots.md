# 2026-07-13 — User manual + generated screenshots (issue #229)

Status: completed (pending first CI screenshot generation)

## What shipped

- **`docs/manual/`** — operator-facing manual organized by workflow:
  getting-started, connect, acquire-and-record, review-and-postprocess,
  troubleshooting, plus an index README. Linked from root `README.md` and
  `docs/README.md`. No developer prerequisites; exact UI labels verified
  against `resources/ui/*.ui`.
- **`screenshot_tour`** (`src/frontend/tools/screenshot_tour_main.cpp`, new
  CMake target in `src/frontend/qt/CMakeLists.txt`) — boots the real
  `AppBackend` + `MainWindow` in mock-camera mode with isolated
  `QTemporaryDir` data/settings, walks all tabs and three settings dialogs,
  and writes `docs/manual/images/<id>.png` + `manifest.json`. Headless via
  `QT_QPA_PLATFORM=offscreen`. See [[../frontend/Screenshot-Tour]].
- **`scripts/check_screenshots.py`** — fails CI when manual image
  references and the harness `kShots[]` registry drift (either direction).
  Wired into `docs-ci.yml`.
- **`build-windows.yml`** — builds + runs the tour after the app build,
  uploads a `manual-screenshots-<version>` artifact, and on release builds
  commits refreshed images back to `main` (tour failure = loud workflow
  warning, not a blocked release).

## Decisions

- Screenshots are **not** hand-captured or checked in from this task; the
  first real set lands when `build-windows.yml` next runs. `check_docs.py`
  ignores image links (its `MD_LINK_RE` skips `![...]`), so pages don't
  break the link checker before generation.
- Repo-root `tools/` is reserved for the standalone Python/packaged tools,
  so the harness lives under `src/frontend/tools/`.
- Stale `mock_studio_qt` references were fixed in passing (`AGENTS.md`,
  [[../build-and-run/Build]], [[../build-and-run/Run-Modes]]) — the target
  no longer exists; mock support lives in `mib_studio_qt`.

## Pitfalls for future agents

- Adding a screenshot = three edits in one PR: `kShots[]`, a tour step,
  and a manual-page image reference — `check_screenshots.py` enforces it.
- The tour finds the main tab widget by object name `"tabs"` and invokes
  private slots `onStartCapture`/`onStopCapture` by name; renames in
  `MainWindow` must update the tour.
