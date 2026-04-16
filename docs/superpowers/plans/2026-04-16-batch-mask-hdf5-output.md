# BatchMaskDialog HDF5 Output Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every batch mask run save a complete standard HDF5 file next to the source and reload HdfReviewTab from it, replacing the fragile in-memory display path.

**Architecture:** Remove the entire "Output" group box from `BatchMaskDialog`. `onRun()` computes an auto-path (`<source_dir>/<stem>_remasked.h5`), prompts before overwriting, calls `saveMasksToHdf5()`, and stores the result in `savedHdf5Path_`. `HdfReviewTab::onRegenerateMasks()` calls `loadHdfFile(dialog.savedHdf5Path())` — giving full scatter plot, histogram, metadata table, and thumbnail support.

**Tech Stack:** C++17, Qt6 Widgets (`QFile`, `QFileInfo`, `QDir`, `QMessageBox`), existing `batch_masks::saveMasksToHdf5()`, `HdfReviewTab::loadHdfFile()`

---

## Files

| Action | Path |
|--------|------|
| Modify | `include/frontend/dialogs/BatchMaskDialog.h` |
| Modify | `src/frontend/dialogs/BatchMaskDialog.cpp` |
| Modify | `src/frontend/tabs/HdfReviewTab.cpp` |

No new files required.

---

### Task 1: Update `BatchMaskDialog.h`

**Files:**
- Modify: `include/frontend/dialogs/BatchMaskDialog.h`

- [ ] **Step 1: Replace the entire header with the new version**

The new header removes: `class QCheckBox;` forward declaration, `displayRequested()` method, `onBrowseOutputPng()` and `onBrowseOutputHdf5()` slots, and the entire `// Output options` member block. It adds: `savedHdf5Path()` getter, `QString savedHdf5Path_` member, `computeAutoOutputPath()` private helper, and updated class comment.

```cpp
#pragma once

#include <QDialog>
#include <QImage>
#include <QRect>
#include <QString>

#include <memory>
#include <vector>

#include "backend/services/ProcessingService.h"
#include "frontend/utils/RoiDrawCanvas.h"

namespace cv { class Mat; }
namespace backend { class AppBackend; }

class QRadioButton;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;
class QProgressBar;
class QPlainTextEdit;
class QDoubleSpinBox;

namespace frontend {

// Dialog for running the batch mask generation pipeline on a range of
// stream images sourced from either an HDF5 file or a folder. On
// completion the results are written to a standard HDF5 file next to
// the source; the saved path is exposed via savedHdf5Path() so the
// caller can reload HdfReviewTab from it.
//
// The right-hand preview panel lets the user visually select an ROI by
// dragging on a source frame, and designate one frame as the background
// image for subtraction. These override the live pipeline values.
class BatchMaskDialog : public QDialog {
    Q_OBJECT
public:
    // `hdf5LoadedPath` is the path of the currently open HDF5 file (if any).
    // When non-empty, the "Current HDF5 frames" radio becomes available.
    explicit BatchMaskDialog(backend::AppBackend& backend,
                             QString hdf5LoadedPath = {},
                             QWidget* parent = nullptr);
    ~BatchMaskDialog() override;

    // After the dialog closes with Accepted:
    // - processedFrames() holds the raw batch results (always populated on success)
    // - savedHdf5Path() holds the path of the written HDF5 file (empty if save failed)
    const std::vector<backend::services::ProcessedFrame>& processedFrames() const { return results_; }
    QString savedHdf5Path() const { return savedHdf5Path_; }

private slots:
    void onSourceChanged();
    void onBrowseFolder();
    void onRun();

    void onPreviewSourceChanged();
    void onPrevFrame();
    void onNextFrame();
    void onSetBackground();
    void onClearBackground();
    void resetConfigToLive();

private:
    void buildUi();
    bool loadInputs(std::vector<cv::Mat>& outGray,
                    std::vector<std::string>& outNames,
                    QString& errorOut);
    void setRunning(bool running);

    void    loadPreviewFrame(int index);
    int     getSourceFrameCount() const;
    QImage  matToQImage(const cv::Mat& gray) const;
    QString computeAutoOutputPath() const;

    backend::AppBackend& backend_;
    QString hdf5LoadedPath_;

    // Source selection
    QRadioButton* srcHdf5_ = nullptr;
    QRadioButton* srcFolder_ = nullptr;
    QLineEdit* folderEdit_ = nullptr;
    QPushButton* folderBrowseBtn_ = nullptr;
    QSpinBox* startIdxSpin_ = nullptr;
    QSpinBox* countSpin_ = nullptr;

    // Status + controls
    QPushButton* runBtn_ = nullptr;
    QPushButton* closeBtn_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;

    // Preview panel
    RoiDrawCanvas* roiCanvas_        = nullptr;
    QLabel*        frameCountLabel_  = nullptr;
    QPushButton*   prevFrameBtn_     = nullptr;
    QPushButton*   nextFrameBtn_     = nullptr;
    QPushButton*   setBgBtn_         = nullptr;
    QPushButton*   clearBgBtn_       = nullptr;
    QLabel*        bgStatusLabel_    = nullptr;

    // Preview state
    int     previewFrameIndex_ = 0;
    int     previewFrameTotal_ = 0;
    cv::Mat backgroundMat_;   // empty = no background subtraction

    // Config panel
    QSpinBox*       blurSpin_        = nullptr;
    QSpinBox*       bgThreshSpin_    = nullptr;
    QSpinBox*       morphKernelSpin_ = nullptr;
    QSpinBox*       morphIterSpin_   = nullptr;
    QSpinBox*       areaMinSpin_     = nullptr;
    QSpinBox*       areaMaxSpin_     = nullptr;
    QDoubleSpinBox* deformMinSpin_   = nullptr;
    QDoubleSpinBox* deformMaxSpin_   = nullptr;

    // Local config — scoped to this batch run, never written back to live pipeline
    backend::services::ProcessingConfig localConfig_;

    std::vector<backend::services::ProcessedFrame> results_;
    QString savedHdf5Path_;
};

} // namespace frontend
```

- [ ] **Step 2: Build header-only to verify no compile errors**

```bash
cmake --build build --config Debug --target mib_backend 2>&1 | grep -E "error:|warning:" | head -20
```

Expected: zero errors (header changes compile cleanly with the backend lib).

---

### Task 2: Update `BatchMaskDialog.cpp`

**Files:**
- Modify: `src/frontend/dialogs/BatchMaskDialog.cpp`

- [ ] **Step 1: Replace the `#include <QCheckBox>` line with `QFile`/`QFileInfo` includes**

Change:
```cpp
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
```
to:
```cpp
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
```

- [ ] **Step 2: Remove the Output group box from `buildUi()`**

Remove the entire block from `// --- Output group ---` through `leftCol->addWidget(outGroup);`:

Remove:
```cpp
    // --- Output group ---
    auto* outGroup  = new QGroupBox(tr("Output"), this);
    auto* outLayout = new QVBoxLayout(outGroup);

    displayCheck_ = new QCheckBox(tr("Display results in review tab"), outGroup);
    displayCheck_->setChecked(true);
    outLayout->addWidget(displayCheck_);

    savePngCheck_ = new QCheckBox(tr("Save mask PNGs to directory"), outGroup);
    outLayout->addWidget(savePngCheck_);
    auto* pngRow = new QHBoxLayout();
    pngRow->addSpacing(20);
    pngDirEdit_ = new QLineEdit(outGroup);
    pngDirEdit_->setEnabled(false);
    pngRow->addWidget(pngDirEdit_);
    pngBrowseBtn_ = new QPushButton(tr("Browse..."), outGroup);
    pngBrowseBtn_->setEnabled(false);
    pngRow->addWidget(pngBrowseBtn_);
    outLayout->addLayout(pngRow);

    saveHdf5Check_ = new QCheckBox(tr("Save masks to HDF5 file"), outGroup);
    outLayout->addWidget(saveHdf5Check_);
    auto* h5Row = new QHBoxLayout();
    h5Row->addSpacing(20);
    hdf5PathEdit_ = new QLineEdit(outGroup);
    hdf5PathEdit_->setEnabled(false);
    h5Row->addWidget(hdf5PathEdit_);
    hdf5BrowseBtn_ = new QPushButton(tr("Browse..."), outGroup);
    hdf5BrowseBtn_->setEnabled(false);
    h5Row->addWidget(hdf5BrowseBtn_);
    outLayout->addLayout(h5Row);

    leftCol->addWidget(outGroup);
```

- [ ] **Step 3: Remove output widget signal connections from `buildUi()`**

Remove these six `connect()` lines:
```cpp
    connect(savePngCheck_,  &QCheckBox::toggled, pngDirEdit_,  &QWidget::setEnabled);
    connect(savePngCheck_,  &QCheckBox::toggled, pngBrowseBtn_,&QWidget::setEnabled);
    connect(pngBrowseBtn_,  &QPushButton::clicked, this, &BatchMaskDialog::onBrowseOutputPng);

    connect(saveHdf5Check_, &QCheckBox::toggled, hdf5PathEdit_,  &QWidget::setEnabled);
    connect(saveHdf5Check_, &QCheckBox::toggled, hdf5BrowseBtn_, &QWidget::setEnabled);
    connect(hdf5BrowseBtn_, &QPushButton::clicked, this, &BatchMaskDialog::onBrowseOutputHdf5);
```

- [ ] **Step 4: Remove `onBrowseOutputPng()` and `onBrowseOutputHdf5()` implementations**

Remove:
```cpp
void BatchMaskDialog::onBrowseOutputPng() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select output directory for mask PNGs"),
        pngDirEdit_->text());
    if (!dir.isEmpty()) pngDirEdit_->setText(dir);
}

void BatchMaskDialog::onBrowseOutputHdf5() {
    const QString file = QFileDialog::getSaveFileName(
        this, tr("Save masks to HDF5 file"),
        hdf5PathEdit_->text(),
        tr("HDF5 files (*.h5 *.hdf5)"));
    if (!file.isEmpty()) hdf5PathEdit_->setText(file);
}
```

- [ ] **Step 5: Replace `setRunning()` — remove output widget lines**

Replace the entire `setRunning()` function with:
```cpp
void BatchMaskDialog::setRunning(bool running) {
    runBtn_->setEnabled(!running);
    srcHdf5_->setEnabled(!running && !hdf5LoadedPath_.isEmpty());
    srcFolder_->setEnabled(!running);
    folderBrowseBtn_->setEnabled(!running && srcFolder_->isChecked());
    prevFrameBtn_->setEnabled(!running && previewFrameIndex_ > 0);
    nextFrameBtn_->setEnabled(!running && previewFrameIndex_ < previewFrameTotal_ - 1);
    setBgBtn_->setEnabled(!running);
    clearBgBtn_->setEnabled(!running);
}
```

- [ ] **Step 6: Replace `onRun()` with new version**

Replace the entire `onRun()` function with:
```cpp
void BatchMaskDialog::onRun() {
    logView_->clear();
    results_.clear();
    savedHdf5Path_.clear();

    const QString outputPath = computeAutoOutputPath();
    if (outputPath.isEmpty()) {
        QMessageBox::warning(this, tr("Cannot determine output path"),
                             tr("Select a source before running."));
        return;
    }

    if (QFile::exists(outputPath)) {
        const auto answer = QMessageBox::question(
            this, tr("Overwrite?"),
            tr("Output file already exists:\n%1\n\nOverwrite?").arg(outputPath),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }

    statusLabel_->setText(tr("Loading images..."));
    progressBar_->setValue(0);
    setRunning(true);

    std::vector<cv::Mat> images;
    std::vector<std::string> names;
    QString err;
    if (!loadInputs(images, names, err)) {
        setRunning(false);
        statusLabel_->setText(tr("Error: %1").arg(err));
        QMessageBox::warning(this, tr("Load failed"), err);
        return;
    }
    logView_->appendPlainText(tr("Loaded %1 images.").arg(images.size()));

    auto& proc        = backend_.processing();
    const auto config = localConfig_;

    const QRect qroi = roiCanvas_->getRoi();
    const backend::services::ProcessingService::Roi roi{
        qroi.x(), qroi.y(), qroi.width(), qroi.height()};
    const cv::Mat background = backgroundMat_;

    progressBar_->setRange(0, static_cast<int>(images.size()));
    statusLabel_->setText(tr("Processing %1 images...").arg(images.size()));

    auto progressCb = [this](const backend::services::ProcessingService::BatchProgress& p) {
        progressBar_->setValue(static_cast<int>(p.done));
        if ((p.done % 25) == 0 || p.done == p.total) {
            QCoreApplication::processEvents();
        }
    };

    results_ = proc.processBatch(images, config, background, roi, progressCb);

    size_t validCount = 0;
    for (const auto& f : results_) if (f.validation.isValid) ++validCount;
    logView_->appendPlainText(
        tr("Processed %1 images: %2 valid, %3 invalid.")
            .arg(results_.size()).arg(validCount).arg(results_.size() - validCount));

    const bool ok = backend::services::batch_masks::saveMasksToHdf5(
        results_, outputPath.toStdString(), config,
        roi.x, roi.y, roi.w, roi.h, background);

    if (ok) {
        savedHdf5Path_ = outputPath;
        logView_->appendPlainText(tr("Saved: %1").arg(outputPath));
        statusLabel_->setText(tr("Done."));
    } else {
        logView_->appendPlainText(tr("HDF5 write FAILED: %1").arg(outputPath));
        statusLabel_->setText(tr("Done (save failed \u2014 see log)."));
    }

    setRunning(false);
}
```

- [ ] **Step 7: Add `computeAutoOutputPath()` implementation**

Add this new function after `onRun()`:
```cpp
QString BatchMaskDialog::computeAutoOutputPath() const {
    if (srcHdf5_->isChecked() && !hdf5LoadedPath_.isEmpty()) {
        const QFileInfo fi(hdf5LoadedPath_);
        return fi.dir().filePath(fi.baseName() + "_remasked.h5");
    }
    const QString folder = folderEdit_->text().trimmed();
    if (folder.isEmpty()) return {};
    const QDir dir(folder);
    const QString name = dir.dirName().isEmpty() ? QStringLiteral("batch") : dir.dirName();
    return dir.filePath(name + "_remasked.h5");
}
```

- [ ] **Step 8: Build to verify**

```bash
cmake --build build --config Debug 2>&1 | grep -E "error:" | head -20
```

Expected: zero errors.

- [ ] **Step 9: Commit**

```bash
git add include/frontend/dialogs/BatchMaskDialog.h src/frontend/dialogs/BatchMaskDialog.cpp
git commit -m "feat(BatchMaskDialog): replace output options with auto-save to standard HDF5"
```

---

### Task 3: Update `HdfReviewTab.cpp`

**Files:**
- Modify: `src/frontend/tabs/HdfReviewTab.cpp`

- [ ] **Step 1: Replace `onRegenerateMasks()` body**

The current method (lines 810–854) checks `dlg.displayRequested()`, splits frames into valid/invalid in-memory vectors, and calls `populateFrames()` / `updateCharts()` directly. Replace the entire body with a call to `loadHdfFile()`:

Replace:
```cpp
void HdfReviewTab::onRegenerateMasks() {
    // Grab the currently loaded HDF5 path (if any) so the dialog can offer
    // "Current HDF5 frames" as a source.
    QString loadedPath;
    if (hdfReader_) {
        const QString label = ui->filePathLabel->text();
        if (label != tr("No file selected")) loadedPath = label;
    }

    BatchMaskDialog dlg(backend_, loadedPath, this);
    if (dlg.exec() != QDialog::Accepted) return;

    if (!dlg.displayRequested()) return;

    // Replace the current in-memory frame set with the batch result so the
    // thumbnail grid refreshes against the newly computed masks.
    const auto& out = dlg.processedFrames();
    if (out.empty()) return;

    std::vector<backend::services::ProcessedFrame> valid, invalid;
    valid.reserve(out.size());
    invalid.reserve(out.size());
    for (const auto& f : out) {
        if (f.validation.isValid) valid.push_back(f);
        else invalid.push_back(f);
    }

    // Reset caches tied to the old dataset indexing.
    thumbnailCache_.clear();
    validThumbnailsLoaded_ = 0;
    invalidThumbnailsLoaded_ = 0;

    validFrames_ = std::move(valid);
    invalidFrames_ = std::move(invalid);

    populateFrames(validFrames_, true);
    populateFrames(invalidFrames_, false);
    updateCharts();

    ui->statusLabel->setText(
        tr("Regenerated masks: %1 valid, %2 invalid")
            .arg(validFrames_.size()).arg(invalidFrames_.size()));
    SPDLOG_INFO("HdfReviewTab: regenerated masks ({} valid, {} invalid)",
                validFrames_.size(), invalidFrames_.size());
}
```

With:
```cpp
void HdfReviewTab::onRegenerateMasks() {
    QString loadedPath;
    if (hdfReader_) {
        const QString label = ui->filePathLabel->text();
        if (label != tr("No file selected")) loadedPath = label;
    }

    BatchMaskDialog dlg(backend_, loadedPath, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString savedPath = dlg.savedHdf5Path();
    if (savedPath.isEmpty()) return;

    loadHdfFile(savedPath);
}
```

- [ ] **Step 2: Build to verify**

```bash
cmake --build build --config Debug 2>&1 | grep -E "error:" | head -20
```

Expected: zero errors.

- [ ] **Step 3: Commit**

```bash
git add src/frontend/tabs/HdfReviewTab.cpp
git commit -m "feat(HdfReviewTab): reload from saved HDF5 after batch mask regeneration"
```

---

### Task 4: Build and Verify

- [ ] **Step 1: Full Debug build**

```bash
cmake --build build --config Debug 2>&1 | tail -5
```

Expected: zero errors. Both `mib_studio_qt` and `mock_studio_qt` targets compile.

- [ ] **Step 2: Launch app and open dialog**

Run `build/Debug/mock_studio_qt.exe`.

Open an HDF5 file in HdfReviewTab → click "Regenerate masks…".

Confirm:
- Dialog has **no Output group box** — only Input source, Preview & ROI, Processing Config, progress, log, Run/Close
- Clicking Run with HDF5 source → log shows `Saved: <path>/<stem>_remasked.h5`
- Clicking Close → HdfReviewTab fully reloads: scatter plot, histogram, metadata table, and thumbnails all populated from the new file

- [ ] **Step 3: Verify overwrite prompt**

Run the dialog a second time with the same source → confirm the overwrite dialog appears.
Click No → run cancelled, no HDF5 written.
Click Yes → file overwritten, HdfReviewTab reloads.

- [ ] **Step 4: Verify folder source**

Set source to a folder of TIFF/PNG images → Run.
Confirm `_remasked.h5` is written inside the folder.
Confirm HdfReviewTab reloads from it.

- [ ] **Step 5: Update vault**

Append to `knowledge_map/current-state/Recent-Work.md`:
```
## 2026-04-16 — BatchMaskDialog always saves standard HDF5
Replaced the "Output" group box (Display / Save PNG / Save HDF5 checkboxes) with a
single auto-save path: `<source_dir>/<stem>_remasked.h5`. After Run, HdfReviewTab
reloads via loadHdfFile() giving full scatter plot, histogram, metadata, and thumbnail
support. Overwrite is prompted. Files: BatchMaskDialog.h/cpp, HdfReviewTab.cpp.
```

- [ ] **Step 6: Commit vault update**

```bash
git add knowledge_map/current-state/Recent-Work.md
git commit -m "docs(vault): update Recent-Work for HDF5 output simplification"
```
