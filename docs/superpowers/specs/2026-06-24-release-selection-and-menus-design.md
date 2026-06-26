# Release selection + menu-bar expansion — design

**Date:** 2026-06-24
**Status:** Approved (brainstorming)

## Problem

Users cannot choose which release to install. The in-app updater
(`AutoUpdater`) is hardwired to the **stable** channel
(`updates.yofo.bio/stable/latest.json`); the channel is only changeable via the
`MIB_STUDIO_UPDATE_MANIFEST_URL` environment variable, and there is no way to
install a *specific* version (e.g. roll back to a prior release, or opt into a
particular beta). Separately, the menu bar (the app's "toolbar") is sparse and
should be rounded out.

Goals:

1. Let a user pick an update **channel** (stable/beta) and a **specific
   version** from the channel's history, including rollback to older versions.
2. Expand the menu bar with useful, discoverable actions.

Non-goals: an icon `QToolBar` (the user wants the menu bar expanded, not a new
icon strip); delta/patch updates; per-user staged rollouts.

## Architecture overview

```
publish-update.py ──writes──> R2: {channel}/latest.json   (unchanged, auto-check)
                              R2: {channel}/index.json     (NEW: full version list)
                                        │
                                        ▼
AutoUpdater (frontend/system) ──reads index.json──> Software Updates dialog
   • channel persisted in QSettings (Update/Channel)
   • fetchVersionIndex(channel)            (new)
   • installSpecificVersion(entry)         (new; reuses download→verify→elevate)
   • checkForUpdates()                      (existing; now channel-aware)
                                        │
UpdateCatalog (pure, header-only) ──parse/sort/mark-current──> tested off-app
```

The download → SHA-256 verify → elevated-install path is **reused verbatim**;
the only new backend surface is "fetch the catalog" and "install a chosen
entry." `latest.json` is untouched, so the existing silent auto-check keeps
working even if `index.json` is absent.

## Components

### 1. Catalog format — `{channel}/index.json`

Newest-first list of every published version in the channel:

```json
{
  "schema_version": 1,
  "channel": "beta",
  "versions": [
    {
      "version": "1.0.4-beta.1",
      "installer_url": "https://updates.yofo.bio/beta/MIB_Studio_Qt_Update_v1.0.4-beta.1.exe",
      "installer_sha256": "abc…",
      "installer_size_bytes": 12345678,
      "release_notes_url": "https://github.com/KPT1020/mib-studio-qt/releases/tag/v1.0.4-beta.1",
      "published_utc": "2026-06-24T04:54:58Z"
    }
  ]
}
```

Each entry carries exactly the fields the existing installer-download path needs
(`installer_url`, `installer_sha256`, `installer_size_bytes`,
`release_notes_url`), plus `version` and `published_utc` for display/sorting.

### 2. `UpdateCatalog` — pure parse/select (header-only)

`include/frontend/utils/UpdateCatalog.h` (+ `.cpp`), namespace
`frontend::updatecatalog`. Pure QtCore, no I/O, unit-testable like
`MindVisionConfig` / `JsonConfigMerge`:

- `struct VersionEntry { QString version, installerUrl, installerSha256Hex,
  releaseNotesUrl, publishedUtc; qint64 installerSizeBytes; }`
- `struct ParseResult { bool ok; QString error; QVector<VersionEntry> versions; }`
- `ParseResult parseIndex(const QByteArray& bytes)` — validates the document is
  an object with a `versions` array; skips malformed entries (missing
  `version`/`installer_url`/`installer_sha256`); returns entries sorted
  newest-first by semantic version (`QVersionNumber`, with `-beta.N` ordered
  before the matching release and by beta number).
- `int indexOfVersion(const QVector<VersionEntry>&, const QString& current)` —
  for marking/《disabling》the currently-installed version.
- `bool isDowngrade(const QString& candidate, const QString& current)` — drives
  the rollback confirmation.

### 3. `AutoUpdater` changes (`src/frontend/system/AutoUpdater.{h,cpp}`)

- **Channel persistence:** read/write `QSettings` key `Update/Channel`
  (`"stable"` | `"beta"`, default `"stable"`). New `channel()` / `setChannel()`.
- **Channel-aware default URL:** `defaultManifestUrl()` →
  `https://updates.yofo.bio/{channel}/latest.json`. The
  `MIB_STUDIO_UPDATE_MANIFEST_URL` env override continues to win (unchanged
  precedence).
- **`indexUrlForChannel(channel)`** → `…/{channel}/index.json`.
- **`fetchVersionIndex()`** — async GET of the channel's `index.json`; on success
  emits `versionIndexReady(QVector<VersionEntry>)`, on failure
  `versionIndexFailed(QString)`. Reuses the existing `QNetworkAccessManager`,
  timeout, and error-formatting helpers.
- **`installSpecificVersion(const VersionEntry&)`** — builds the existing
  `Manifest` struct from the entry and calls the current
  `startInstallerDownload(...)` path (download → size/SHA-256 verify → elevated
  run). No new download/verify/elevation logic.
- `checkForUpdates()` is unchanged apart from now resolving the channel.

### 4. UI — "Software Updates" dialog

New `SoftwareUpdatesDialog` (`src/frontend/dialogs/…` + `resources/ui/…`),
opened from **Help ▸ Software Updates…** (replaces "Check for Updates…"):

- **Channel** dropdown (Stable / Beta) bound to `AutoUpdater::channel()`;
  changing it persists the setting and reloads the version list.
- **Available versions** list from `index.json`: `version` + `published_utc`,
  with the installed version marked "(current)" and non-installable.
- Buttons: **Install Selected**, **Release Notes** (opens
  `release_notes_url`), **Check for Latest** (existing
  `checkForUpdates(interactive=true)` flow).
- Selecting an older version → **Install** prompts a downgrade confirmation
  (`UpdateCatalog::isDowngrade`).
- States: loading, empty/unavailable index (falls back to "use Check for
  Latest"), parse error, offline.

The legacy startup auto-check is unchanged; only the manual entry point becomes
this dialog.

### 5. Menu-bar expansion (`resources/ui/MainWindow.ui` + `MainWindow.cpp`)

- **File:** *Open Data Folder*, *Open Logs Folder*
  (`%LOCALAPPDATA%\MIB_Studio_Qt\{data,logs}` via `QDesktopServices`), Exit.
- **Settings:** existing actions + *Profiles…* (opens the existing
  ProfileManager/ConfigTabs surface).
- **Help:** About, **Software Updates…** (new), *Documentation* (opens the docs
  URL), *Report a Problem* (opens the GitHub issues URL).

New actions are thin wrappers over existing functionality or `QDesktopServices`
calls; no new subsystems.

### 6. `publish-update.py` changes

After the existing installer + `latest.json` upload, maintain `index.json`:

1. Download the channel's current `index.json` (empty skeleton if 404).
2. Build the new `VersionEntry` from the just-published artifact (version,
   url, sha256, size, notes url, UTC timestamp).
3. Insert/replace by `version` (dedupe), keep newest-first, re-upload with the
   same short-TTL cache headers as `latest.json`.

Factor the merge into a pure function `merge_index(existing: dict, entry: dict)
-> dict` so it is unit-testable. A `--backfill-index` one-shot seeds
`index.json` for a channel from the existing GitHub releases (so the list isn't
empty on first ship).

## Error handling

- `index.json` 404/empty → dialog shows "version history unavailable," offers
  "Check for Latest" only; auto-check unaffected.
- Malformed `index.json` → parse error surfaced; no crash; individual bad
  entries skipped rather than failing the whole list.
- Install of selected version → existing size + SHA-256 verification gates the
  run; mismatch aborts with a clear message.
- Downgrade → explicit confirmation before proceeding.
- Selected version == installed → Install disabled.

## Testing

- **`tests/frontend/update_catalog_test.cpp`** (new target, `Qt6::Core`, like
  `json_config_merge_test`): parse valid/empty/malformed index; newest-first
  ordering incl. `-beta.N`; skip-bad-entry; `indexOfVersion`; `isDowngrade`
  (incl. beta vs release edge cases).
- **Channel→URL mapping** assertions in the same target (latest.json + index.json
  for stable/beta; env override precedence).
- **`tests/tools/test_publish_index_merge.py`** (or inline) for
  `merge_index`: insert-new, dedupe-existing, ordering, 404-skeleton.
- Manual: dialog flow on Windows (channel switch, list load, install, rollback
  confirm, release-notes link).

## Vault / docs

- `knowledge_map/frontend/System-Utilities.md` — `AutoUpdater` channel/version
  selection + `UpdateCatalog`.
- `knowledge_map/frontend/` — note the new `SoftwareUpdatesDialog` and menu
  actions (MainWindow/menus note).
- `docs/howto/auto-update-r2.md` + `docs/howto/release-workflow.md` —
  document `index.json`, the publish step, and `--backfill-index`.

## Build sequence

1. `UpdateCatalog` header/cpp + test (pure, no UI/SDK) — landable first.
2. `AutoUpdater` channel + index fetch + install-specific (backend).
3. `SoftwareUpdatesDialog` + Help menu wiring.
4. Menu-bar expansion (File/Settings/Help).
5. `publish-update.py` index maintenance + backfill + Python test.
6. Docs/vault updates.
