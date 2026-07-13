# 2026-07-13 — User manual + generated screenshots (issue #229)

Status: completed

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

- The initial screenshot set **is** committed, generated on Linux via the
  `linux-system-release` preset (`QT_QPA_PLATFORM=offscreen`); the release
  workflow keeps it fresh from then on. `check_docs.py` ignores image links
  (its `MD_LINK_RE` skips `![...]`), so pages never depend on the images
  existing.
- UI sources moved into a `mib_frontend_common` STATIC library so the app
  and the tour compile them once. AUTOUIC must be `OFF` on the executables
  (duplicate `ui_*.h` ninja rules otherwise) and the `.qrc` is listed per
  executable (Qt resources in a static library are dropped without
  `Q_INIT_RESOURCE`).
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
