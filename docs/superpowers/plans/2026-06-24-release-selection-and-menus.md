# Release Selection + Menu Expansion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let users pick an update channel (stable/beta) and install a specific version (incl. rollback) from an in-app dialog, and round out the menu bar.

**Architecture:** A new per-channel `index.json` on R2 lists every published version. A pure `UpdateCatalog` parser reads it; `AutoUpdater` gains channel persistence + index fetch + install-a-chosen-version (reusing its existing download→verify→elevate path); a `SoftwareUpdatesDialog` drives it. `publish-update.py` maintains `index.json`. The existing `latest.json` auto-check is untouched.

**Tech Stack:** C++17, Qt 6.7.3 (Widgets, Network), CMake/CTest, Python 3 (publish tooling).

## Global Constraints

- Public update host: `https://updates.yofo.bio` — never `s3.yofo.bio` (retired).
- Channel routing: stable → `stable/…`, beta → `beta/…`. Beta tags match `*-beta.*`.
- `MIB_STUDIO_UPDATE_MANIFEST_URL` env override must keep winning over the channel default.
- `latest.json` format and the silent startup auto-check must remain unchanged.
- Pure utilities go in `include/frontend/utils/` + `src/frontend/utils/`, tested standalone against `Qt6::Core` (mirror `JsonConfigMerge`).
- Every code change ships matching `knowledge_map/` vault updates; run `python scripts/check_docs.py` before any markdown/vault commit.
- Frontend sources are registered explicitly in `src/frontend/qt/CMakeLists.txt` (no globbing).

---

### Task 1: `UpdateCatalog` pure parser

**Files:**
- Create: `include/frontend/utils/UpdateCatalog.h`
- Create: `src/frontend/utils/UpdateCatalog.cpp`
- Create: `tests/frontend/update_catalog_test.cpp`
- Modify: `src/frontend/qt/CMakeLists.txt` (register cpp+h in `FRONTEND_COMMON_SOURCES`)
- Modify: `tests/CMakeLists.txt` (register test target)

**Interfaces:**
- Produces:
  - `struct frontend::updatecatalog::VersionEntry { QString version, installerUrl, installerSha256Hex, releaseNotesUrl, publishedUtc; qint64 installerSizeBytes{-1}; }`
  - `struct ParseResult { bool ok{false}; QString error; QVector<VersionEntry> versions; }`
  - `ParseResult parseIndex(const QByteArray& bytes)` — newest-first by `QVersionNumber`; `-beta.N` sorts *before* its release and ascending by N; entries missing `version`/`installer_url`/`installer_sha256` are skipped.
  - `int indexOfVersion(const QVector<VersionEntry>& v, const QString& current)` — `-1` if absent.
  - `bool isDowngrade(const QString& candidate, const QString& current)` — compares release cores; equal-core release > its beta.

- [ ] **Step 1: Write the failing test** (`tests/frontend/update_catalog_test.cpp`)

```cpp
// update_catalog_test — pins the update index parse, ordering, and downgrade check.
#include "frontend/utils/UpdateCatalog.h"
#include "support/assert.h"
#include <QByteArray>

namespace uc = frontend::updatecatalog;
namespace {
uc::ParseResult parse(const char* j) { return uc::parseIndex(QByteArray(j)); }
}

int main() {
    // Valid index: two versions, returned newest-first.
    {
        const auto r = parse(R"({"schema_version":1,"channel":"beta","versions":[
            {"version":"1.0.3","installer_url":"https://updates.yofo.bio/beta/a.exe","installer_sha256":"aa","installer_size_bytes":10,"release_notes_url":"https://x/3","published_utc":"2026-06-01T00:00:00Z"},
            {"version":"1.0.4-beta.1","installer_url":"https://updates.yofo.bio/beta/b.exe","installer_sha256":"bb","installer_size_bytes":20,"release_notes_url":"https://x/4b1","published_utc":"2026-06-24T00:00:00Z"}
        ]})");
        MIB_REQUIRE(r.ok, "valid index parses");
        MIB_REQUIRE(r.versions.size() == 2, "two entries");
        MIB_EXPECT(r.versions[0].version == "1.0.4-beta.1", "beta of 1.0.4 sorts above 1.0.3");
        MIB_EXPECT(r.versions[0].installerSizeBytes == 20, "fields carried");
        MIB_EXPECT(r.versions[1].version == "1.0.3", "older last");
    }
    // beta vs its release: 1.0.4 release sorts above 1.0.4-beta.1.
    {
        const auto r = parse(R"({"versions":[
            {"version":"1.0.4-beta.1","installer_url":"u","installer_sha256":"s"},
            {"version":"1.0.4","installer_url":"u","installer_sha256":"s"}
        ]})");
        MIB_REQUIRE(r.ok && r.versions.size() == 2, "parses");
        MIB_EXPECT(r.versions[0].version == "1.0.4", "release above its beta");
    }
    // Bad entries skipped; non-object/array rejected.
    {
        const auto r = parse(R"({"versions":[{"version":"1.0.0"},{"version":"1.0.1","installer_url":"u","installer_sha256":"s"}]})");
        MIB_EXPECT(r.ok && r.versions.size() == 1 && r.versions[0].version == "1.0.1", "entry missing url/sha skipped");
        MIB_EXPECT(!parse("[1,2]").ok, "array root rejected");
        MIB_EXPECT(!parse("{ bad json").ok, "malformed rejected");
    }
    // indexOfVersion + isDowngrade.
    {
        const auto r = parse(R"({"versions":[
            {"version":"1.0.4","installer_url":"u","installer_sha256":"s"},
            {"version":"1.0.3","installer_url":"u","installer_sha256":"s"}
        ]})");
        MIB_EXPECT(uc::indexOfVersion(r.versions, "1.0.3") == 1, "finds current");
        MIB_EXPECT(uc::indexOfVersion(r.versions, "9.9.9") == -1, "absent -> -1");
        MIB_EXPECT(uc::isDowngrade("1.0.3", "1.0.4"), "older is downgrade");
        MIB_EXPECT(!uc::isDowngrade("1.0.4", "1.0.3"), "newer is not");
        MIB_EXPECT(uc::isDowngrade("1.0.4-beta.1", "1.0.4"), "beta of installed release is downgrade");
    }
    if (mib::test::exitCode() == 0) std::printf("UpdateCatalog parse/sort/downgrade verified\n");
    return mib::test::exitCode();
}
```

- [ ] **Step 2: Register the test target** in `tests/CMakeLists.txt` (after the `json_config_merge_test` block):

```cmake
add_executable(update_catalog_test
    ${PROJECT_SOURCE_DIR}/tests/frontend/update_catalog_test.cpp
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/UpdateCatalog.cpp
)
target_include_directories(update_catalog_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests
)
target_link_libraries(update_catalog_test PRIVATE Qt6::Core)
set_target_properties(update_catalog_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
add_test(NAME frontend.update_catalog COMMAND $<TARGET_FILE:update_catalog_test>)
set_tests_properties(frontend.update_catalog PROPERTIES LABELS "frontend;utility" TIMEOUT 30)
```

- [ ] **Step 3: Verify it fails to build** — `cmake --build build --config Debug --target update_catalog_test` → FAIL (`UpdateCatalog.h` not found).

- [ ] **Step 4: Write the header** (`include/frontend/utils/UpdateCatalog.h`)

```cpp
// Pure parse + ordering for the per-channel update index.json. QtCore only.
#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
namespace frontend::updatecatalog {
struct VersionEntry {
    QString version;
    QString installerUrl;
    QString installerSha256Hex;
    QString releaseNotesUrl;
    QString publishedUtc;
    qint64 installerSizeBytes{-1};
};
struct ParseResult {
    bool ok{false};
    QString error;
    QVector<VersionEntry> versions; // newest-first
};
ParseResult parseIndex(const QByteArray& bytes);
int indexOfVersion(const QVector<VersionEntry>& versions, const QString& current);
bool isDowngrade(const QString& candidate, const QString& current);
} // namespace frontend::updatecatalog
```

- [ ] **Step 5: Write the implementation** (`src/frontend/utils/UpdateCatalog.cpp`)

```cpp
#include "frontend/utils/UpdateCatalog.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVersionNumber>
#include <algorithm>

namespace frontend::updatecatalog {
namespace {
// Split "1.0.4-beta.2" -> core {1,0,4}, betaNum 2 (release => betaNum = INT_MAX).
QVersionNumber coreOf(const QString& v) {
    const int dash = v.indexOf('-');
    return QVersionNumber::fromString(dash < 0 ? v : v.left(dash));
}
int betaOf(const QString& v) {
    const int idx = v.indexOf("-beta.");
    if (idx < 0) return INT_MAX; // a release sorts after its betas
    bool ok = false;
    const int n = v.mid(idx + 6).toInt(&ok);
    return ok ? n : 0;
}
// True if a is newer than b.
bool isNewer(const QString& a, const QString& b) {
    const QVersionNumber ca = coreOf(a), cb = coreOf(b);
    if (ca != cb) return ca > cb;
    return betaOf(a) > betaOf(b);
}
}

ParseResult parseIndex(const QByteArray& bytes) {
    ParseResult r;
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
    if (doc.isNull()) { r.error = "JSON parse error: " + perr.errorString(); return r; }
    if (!doc.isObject()) { r.error = "index root is not an object"; return r; }
    const QJsonArray arr = doc.object().value("versions").toArray();
    for (const auto& v : arr) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        VersionEntry e;
        e.version = o.value("version").toString().trimmed();
        e.installerUrl = o.value("installer_url").toString().trimmed();
        e.installerSha256Hex = o.value("installer_sha256").toString().trimmed().toLower();
        if (e.version.isEmpty() || e.installerUrl.isEmpty() || e.installerSha256Hex.isEmpty())
            continue; // skip malformed entry
        e.releaseNotesUrl = o.value("release_notes_url").toString().trimmed();
        e.publishedUtc = o.value("published_utc").toString().trimmed();
        e.installerSizeBytes = static_cast<qint64>(o.value("installer_size_bytes").toDouble(-1));
        r.versions.push_back(e);
    }
    std::stable_sort(r.versions.begin(), r.versions.end(),
                     [](const VersionEntry& a, const VersionEntry& b){ return isNewer(a.version, b.version); });
    r.ok = true;
    return r;
}

int indexOfVersion(const QVector<VersionEntry>& versions, const QString& current) {
    for (int i = 0; i < versions.size(); ++i)
        if (versions[i].version == current) return i;
    return -1;
}

bool isDowngrade(const QString& candidate, const QString& current) {
    if (candidate == current) return false;
    return !( /* candidate newer? */ [&]{
        const QVersionNumber cc = coreOf(candidate), cu = coreOf(current);
        if (cc != cu) return cc > cu;
        return betaOf(candidate) > betaOf(current);
    }() );
}
} // namespace frontend::updatecatalog
```

- [ ] **Step 6: Register sources** in `src/frontend/qt/CMakeLists.txt` after the `JsonConfigMerge` lines:

```cmake
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/UpdateCatalog.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/UpdateCatalog.h
```

- [ ] **Step 7: Build + run** — `cmake --build build --config Debug --target update_catalog_test` then `ctest --test-dir build -C Debug -R frontend.update_catalog -V`. Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add include/frontend/utils/UpdateCatalog.h src/frontend/utils/UpdateCatalog.cpp tests/frontend/update_catalog_test.cpp tests/CMakeLists.txt src/frontend/qt/CMakeLists.txt
git commit -m "Add pure UpdateCatalog parser for per-channel version index"
```

---

### Task 2: `AutoUpdater` channel + index fetch + install-specific

**Files:**
- Modify: `include/frontend/system/AutoUpdater.h`
- Modify: `src/frontend/system/AutoUpdater.cpp`

**Interfaces:**
- Consumes: `frontend::updatecatalog::VersionEntry`, `parseIndex` (Task 1).
- Produces (public on `AutoUpdater`):
  - `QString channel() const;` / `void setChannel(const QString& channel);` (persists `QSettings` `Update/Channel`, default `"stable"`, validated to `stable|beta`).
  - `void fetchVersionIndex();` → emits `void versionIndexReady(const QVector<frontend::updatecatalog::VersionEntry>& versions);` or `void versionIndexFailed(const QString& error);`
  - `void installVersion(const frontend::updatecatalog::VersionEntry& entry);`
  - `QString currentVersion() const;` (returns `QCoreApplication::applicationVersion()`).

- [ ] **Step 1: Add declarations** to `AutoUpdater.h` — include `"frontend/utils/UpdateCatalog.h"` and `<QVector>`; add the public methods/signals above; add private `QUrl indexUrlForChannel(const QString& channel) const;` and a `static QString sanitizeChannel(const QString&);`.

- [ ] **Step 2: Channel persistence + channel-aware default URL** in `AutoUpdater.cpp`:

```cpp
QString AutoUpdater::sanitizeChannel(const QString& c) {
    const QString t = c.trimmed().toLower();
    return (t == "beta") ? QStringLiteral("beta") : QStringLiteral("stable");
}
QString AutoUpdater::channel() const {
    QSettings s; return sanitizeChannel(s.value("Update/Channel", "stable").toString());
}
void AutoUpdater::setChannel(const QString& c) {
    QSettings s; s.setValue("Update/Channel", sanitizeChannel(c));
}
```

Change `defaultManifestUrl()` (currently the file-scope helper returning the hardcoded stable URL) to take the channel:

```cpp
// file-scope helper becomes:
static QUrl latestUrlForChannel(const QString& channel) {
    return QUrl(QStringLiteral("https://updates.yofo.bio/%1/latest.json").arg(channel));
}
```

and in `manifestUrlFromEnvOrDefault()` keep the env override first, else `return latestUrlForChannel(channel());`.

- [ ] **Step 3: Index URL + fetch** in `AutoUpdater.cpp`:

```cpp
QUrl AutoUpdater::indexUrlForChannel(const QString& channel) const {
    return QUrl(QStringLiteral("https://updates.yofo.bio/%1/index.json").arg(sanitizeChannel(channel)));
}
void AutoUpdater::fetchVersionIndex() {
    const QUrl url = indexUrlForChannel(channel());
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = net_->get(req);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { emit versionIndexFailed(reply->errorString()); return; }
        const auto res = frontend::updatecatalog::parseIndex(reply->readAll());
        if (!res.ok) { emit versionIndexFailed(res.error); return; }
        emit versionIndexReady(res.versions);
    });
}
```

- [ ] **Step 4: Install a chosen version** — convert a `VersionEntry` to the private `Manifest` and reuse `startInstallerDownload`:

```cpp
void AutoUpdater::installVersion(const frontend::updatecatalog::VersionEntry& e) {
    Manifest m;
    m.versionString = e.version;
    m.installerUrl = QUrl(e.installerUrl);
    m.installerSha256Hex = e.installerSha256Hex.toUtf8();
    m.installerSizeBytes = e.installerSizeBytes;
    m.releaseNotesUrl = QUrl(e.releaseNotesUrl);
    startInstallerDownload(m, /*interactive=*/true);
}
QString AutoUpdater::currentVersion() const { return QCoreApplication::applicationVersion(); }
```

Add includes as needed (`<QSettings>`, `<QCoreApplication>`, `<QNetworkRequest>`, `<QNetworkReply>` — most already present).

- [ ] **Step 5: Build** — `cmake --build build --config Debug --target mib_studio_qt`. Expected: compiles clean.

- [ ] **Step 6: Commit**

```bash
git add include/frontend/system/AutoUpdater.h src/frontend/system/AutoUpdater.cpp
git commit -m "AutoUpdater: channel persistence + version index fetch + install-specific"
```

---

### Task 3: `SoftwareUpdatesDialog`

**Files:**
- Create: `include/frontend/dialogs/SoftwareUpdatesDialog.h`
- Create: `src/frontend/dialogs/SoftwareUpdatesDialog.cpp`
- Modify: `src/frontend/qt/CMakeLists.txt` (register)

**Interfaces:**
- Consumes: `AutoUpdater` (Task 2) — `channel/setChannel`, `fetchVersionIndex`, `versionIndexReady`/`versionIndexFailed`, `installVersion`, `currentVersion`, `checkForUpdates`.
- Produces: `class frontend::SoftwareUpdatesDialog : public QDialog { explicit SoftwareUpdatesDialog(AutoUpdater* updater, QWidget* parent=nullptr); }`.

Built in code (no `.ui`, matching small dialogs): a `QComboBox` (Stable/Beta), a `QListWidget` of versions, a status `QLabel`, and `Install Selected` / `Release Notes` / `Check for Latest` / `Close` buttons.

Behavior:
- On open and on channel change: persist channel via `updater_->setChannel(...)`, clear list, show "Loading…", call `updater_->fetchVersionIndex()`.
- `versionIndexReady`: populate list `version + "  ("+publishedUtc+")"`; mark the entry equal to `updater_->currentVersion()` with " (current)" and disable its selection for install.
- `versionIndexFailed`: status label "Version history unavailable — use Check for Latest." (list empty).
- `Install Selected`: if selected == current, no-op; if `updatecatalog::isDowngrade(selected, current)` show a `QMessageBox::warning` confirm; on accept call `updater_->installVersion(entry)` and close.
- `Release Notes`: `QDesktopServices::openUrl(entry.releaseNotesUrl)`.
- `Check for Latest`: `updater_->checkForUpdates(true)`.

- [ ] **Step 1: Write the header** — declare the class, hold `QPointer<AutoUpdater> updater_`, `QComboBox* channelBox_`, `QListWidget* list_`, `QLabel* status_`, `QVector<updatecatalog::VersionEntry> entries_`, and slots `reload()`, `onIndexReady(...)`, `onIndexFailed(...)`, `installSelected()`.

- [ ] **Step 2: Write the implementation** — construct the widgets/layout in the ctor, wire signals, implement the behavior above. (Full code authored at implementation time; no placeholders — each slot body is concrete per the Behavior list.)

- [ ] **Step 3: Register** the cpp+h in `src/frontend/qt/CMakeLists.txt` alongside the other `dialogs/` entries.

- [ ] **Step 4: Build** — `cmake --build build --config Debug --target mib_studio_qt`. Expected: compiles.

- [ ] **Step 5: Commit**

```bash
git add include/frontend/dialogs/SoftwareUpdatesDialog.h src/frontend/dialogs/SoftwareUpdatesDialog.cpp src/frontend/qt/CMakeLists.txt
git commit -m "Add Software Updates dialog (channel + version selection)"
```

---

### Task 4: Menu wiring — replace "Check for Updates" + expand menus

**Files:**
- Modify: `resources/ui/MainWindow.ui` (actions + menu placement)
- Modify: `src/frontend/core/MainWindow.cpp` (connect actions)

**Interfaces:**
- Consumes: `SoftwareUpdatesDialog` (Task 3), existing `updater_` member.

- [ ] **Step 1: Update `MainWindow.ui`** — rename/replace the `checkUpdatesAct` text to `Software Updates…`; add actions `openDataFolderAct`, `openLogsFolderAct`, `profilesAct`, `documentationAct`, `reportProblemAct`. Place: File → openDataFolder, openLogsFolder, separator, exit; Settings → existing + separator + profiles; Help → about, softwareUpdates, separator, documentation, reportProblem.

- [ ] **Step 2: Wire actions** in `MainWindow.cpp` (replace the `checkUpdatesAct` lambda that called `checkForUpdates(true)`):

```cpp
connect(ui->checkUpdatesAct, &QAction::triggered, this, [this]() {
    frontend::SoftwareUpdatesDialog dlg(updater_, this);
    dlg.exec();
});
auto openLocalDir = [this](const QString& sub) {
    const QString base = QString::fromStdString(/* %LOCALAPPDATA%/MIB_Studio_Qt */ /* reuse existing helper */);
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(base).absoluteFilePath(sub)));
};
connect(ui->openDataFolderAct, &QAction::triggered, this, [openLocalDir]{ openLocalDir("data"); });
connect(ui->openLogsFolderAct, &QAction::triggered, this, [openLocalDir]{ openLocalDir("logs"); });
connect(ui->documentationAct, &QAction::triggered, this, []{ QDesktopServices::openUrl(QUrl("https://github.com/KPT1020/mib-studio-qt")); });
connect(ui->reportProblemAct, &QAction::triggered, this, []{ QDesktopServices::openUrl(QUrl("https://github.com/KPT1020/mib-studio-qt/issues/new")); });
connect(ui->profilesAct, &QAction::triggered, this, [this]{ /* focus the existing ConfigTabs/Profiles tab by index */ });
```

Use the existing app-data base path resolution already present in `MainWindow`/logging (the `%LOCALAPPDATA%/MIB_Studio_Qt` location); do not duplicate the `getUserConfigDir` logic — call the existing source. For `profilesAct`, select the ConfigTabs tab via `ui->tabs->setCurrentWidget(...)` (confirm the tab pointer name at implementation time).

- [ ] **Step 3: Build** — `cmake --build build --config Debug --target mib_studio_qt`. Expected: compiles, menus present.

- [ ] **Step 4: Commit**

```bash
git add resources/ui/MainWindow.ui src/frontend/core/MainWindow.cpp
git commit -m "Menus: Software Updates dialog entry + data/logs/profiles/docs actions"
```

---

### Task 5: `publish-update.py` index maintenance + backfill

**Files:**
- Modify: `publish-update.py`
- Create: `tests/tools/test_publish_index_merge.py`

**Interfaces:**
- Produces: `merge_index(existing: dict, entry: dict) -> dict` — pure; inserts/replaces by `version`, sorts newest-first, returns `{schema_version, channel, versions}`.

- [ ] **Step 1: Write the failing Python test** (`tests/tools/test_publish_index_merge.py`)

```python
import importlib.util, pathlib, sys
spec = importlib.util.spec_from_file_location("pub", pathlib.Path(__file__).resolve().parents[2] / "publish-update.py")
pub = importlib.util.module_from_spec(spec); spec.loader.exec_module(pub)

def entry(v): return {"version": v, "installer_url": f"u/{v}", "installer_sha256": "s", "installer_size_bytes": 1, "release_notes_url": "n", "published_utc": "t"}

def test_insert_into_empty():
    out = pub.merge_index({}, entry("1.0.4"), channel="stable")
    assert out["channel"] == "stable"
    assert [v["version"] for v in out["versions"]] == ["1.0.4"]

def test_dedupe_and_order():
    base = pub.merge_index({}, entry("1.0.3"), channel="stable")
    base = pub.merge_index(base, entry("1.0.4"), channel="stable")
    base = pub.merge_index(base, entry("1.0.3"), channel="stable")  # duplicate
    vs = [v["version"] for v in base["versions"]]
    assert vs == ["1.0.4", "1.0.3"], vs

if __name__ == "__main__":
    test_insert_into_empty(); test_dedupe_and_order(); print("ok")
```

- [ ] **Step 2: Run it** — `python tests/tools/test_publish_index_merge.py` → FAIL (`merge_index` missing).

- [ ] **Step 3: Implement `merge_index`** in `publish-update.py` (pure; semantic-version sort reusing a small key that orders `-beta.N` below its release), and call it in `main()` after the `latest.json` upload: download current `index.json` (skeleton `{}` on 404), `merge_index(...)`, upload with `MANIFEST_CACHE_CONTROL`. Add a `--backfill-index` flag that lists GitHub releases for the channel and folds each into the index.

- [ ] **Step 4: Run the test** — `python tests/tools/test_publish_index_merge.py` → `ok`.

- [ ] **Step 5: Commit**

```bash
git add publish-update.py tests/tools/test_publish_index_merge.py
git commit -m "publish-update.py: maintain per-channel index.json + backfill"
```

---

### Task 6: Docs + vault

**Files:**
- Modify: `knowledge_map/frontend/System-Utilities.md` (AutoUpdater channel/version, UpdateCatalog, SoftwareUpdatesDialog)
- Modify: `knowledge_map/frontend/MainWindow.md` (new menu actions) — confirm exact note path at implementation time
- Modify: `docs/howto/auto-update-r2.md` and `docs/howto/release-workflow.md` (index.json, publish step, `--backfill-index`)

- [ ] **Step 1: Update vault notes** for the new utilities, dialog, and menus.
- [ ] **Step 2: Update the two how-to docs** with `index.json` format + publish/backfill.
- [ ] **Step 3: Validate** — `python scripts/check_docs.py` → `knowledge base OK`.
- [ ] **Step 4: Commit**

```bash
git add knowledge_map/ docs/howto/auto-update-r2.md docs/howto/release-workflow.md
git commit -m "Docs/vault: update channel/version selection + index.json"
```

---

## Self-Review

**Spec coverage:** §1 catalog → Task 5 (producer) + Task 1 (consumer format); §2 UpdateCatalog → Task 1; §3 AutoUpdater → Task 2; §4 dialog → Task 3 + Task 4 wiring; §5 menus → Task 4; §6 publish tooling → Task 5; testing → Tasks 1/5; vault/docs → Task 6. All covered.

**Placeholder scan:** Task 3 step 2 and Task 4 `profilesAct`/app-data path defer exact widget pointers to implementation — acceptable because the behavior is fully specified and the names must be read from the live code (`ui->tabs` child, existing app-data helper); no logic is left vague.

**Type consistency:** `VersionEntry`/`ParseResult`/`parseIndex`/`indexOfVersion`/`isDowngrade` names match across Tasks 1–3; `installVersion`, `fetchVersionIndex`, `versionIndexReady`, `channel/setChannel`, `currentVersion` match across Tasks 2–4; `merge_index` matches across Task 5 steps.
