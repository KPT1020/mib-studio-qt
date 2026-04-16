# BatchMaskDialog Config Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a third "Processing Config" column to `BatchMaskDialog` exposing 8 key `ProcessingConfig` fields that apply only to the current batch run.

**Architecture:** `BatchMaskDialog` gains a `localConfig_` (`ProcessingConfig`) member pre-populated from the live pipeline on open. Eight spinboxes update `localConfig_` directly on change. `onRun()` uses `localConfig_` instead of freshly reading from `getProcessingConfig()`. A "Reset" button re-reads the live config.

**Tech Stack:** C++17, Qt6 Widgets (`QSpinBox`, `QDoubleSpinBox`, `QGroupBox`, `QFormLayout`), `ProcessingConfig` from `backend/services/ProcessingService.h`

---

## Files

| Action | Path |
|--------|------|
| Modify | `include/frontend/dialogs/BatchMaskDialog.h` |
| Modify | `src/frontend/dialogs/BatchMaskDialog.cpp` |

No new files required.

---

### Task 1: Update `BatchMaskDialog.h`

**Files:**
- Modify: `include/frontend/dialogs/BatchMaskDialog.h`

- [ ] **Step 1: Add `QDoubleSpinBox` forward declaration**

After the existing `class QPlainTextEdit;` line add:
```cpp
class QDoubleSpinBox;
```

- [ ] **Step 2: Add `resetConfigToLive()` to private slots**

In the `private slots:` block, after `void onClearBackground();` add:
```cpp
    void resetConfigToLive();
```

- [ ] **Step 3: Add config spinbox members**

After the `// Preview state` block, add a new `// Config panel` block:
```cpp
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
```

---

### Task 2: Update `BatchMaskDialog.cpp`

**Files:**
- Modify: `src/frontend/dialogs/BatchMaskDialog.cpp`

- [ ] **Step 1: Add `QDoubleSpinBox` include**

After the `#include <QSpinBox>` line add:
```cpp
#include <QDoubleSpinBox>
```

- [ ] **Step 2: Update dialog width in constructor**

Change:
```cpp
    resize(950, 540);
```
to:
```cpp
    resize(1180, 540);
```

- [ ] **Step 3: Call `resetConfigToLive()` in constructor after `buildUi()`**

Change:
```cpp
    buildUi();
    onSourceChanged();
    onPreviewSourceChanged();
```
to:
```cpp
    buildUi();
    resetConfigToLive();
    onSourceChanged();
    onPreviewSourceChanged();
```

- [ ] **Step 4: Add config group box to `buildUi()` — third column**

At the end of the `buildUi()` function, just before the line:
```cpp
    root->addLayout(topRow);
```
insert the following block (after `topRow->addWidget(previewGroup, 1);`):

```cpp
    // --- Processing Config group (third column) ---
    auto* configGroup  = new QGroupBox(tr("Processing Config"), this);
    configGroup->setFixedWidth(230);
    auto* configLayout = new QVBoxLayout(configGroup);

    // Image Processing sub-group
    auto* imgGroup  = new QGroupBox(tr("Image Processing"), configGroup);
    auto* imgForm   = new QFormLayout(imgGroup);

    blurSpin_ = new QSpinBox(imgGroup);
    blurSpin_->setRange(1, 99);
    blurSpin_->setSingleStep(2);
    imgForm->addRow(tr("Blur kernel:"), blurSpin_);

    bgThreshSpin_ = new QSpinBox(imgGroup);
    bgThreshSpin_->setRange(0, 255);
    imgForm->addRow(tr("BG threshold:"), bgThreshSpin_);

    morphKernelSpin_ = new QSpinBox(imgGroup);
    morphKernelSpin_->setRange(1, 99);
    morphKernelSpin_->setSingleStep(2);
    imgForm->addRow(tr("Morph kernel:"), morphKernelSpin_);

    morphIterSpin_ = new QSpinBox(imgGroup);
    morphIterSpin_->setRange(1, 20);
    imgForm->addRow(tr("Morph iters:"), morphIterSpin_);

    configLayout->addWidget(imgGroup);

    // Validation sub-group
    auto* valGroup = new QGroupBox(tr("Validation"), configGroup);
    auto* valForm  = new QFormLayout(valGroup);

    areaMinSpin_ = new QSpinBox(valGroup);
    areaMinSpin_->setRange(0, 10000);
    areaMinSpin_->setSuffix(tr(" \u03bcm\u00b2"));
    valForm->addRow(tr("Area min:"), areaMinSpin_);

    areaMaxSpin_ = new QSpinBox(valGroup);
    areaMaxSpin_->setRange(0, 10000);
    areaMaxSpin_->setSuffix(tr(" \u03bcm\u00b2"));
    valForm->addRow(tr("Area max:"), areaMaxSpin_);

    deformMinSpin_ = new QDoubleSpinBox(valGroup);
    deformMinSpin_->setRange(0.0, 1.0);
    deformMinSpin_->setSingleStep(0.001);
    deformMinSpin_->setDecimals(4);
    valForm->addRow(tr("Deform min:"), deformMinSpin_);

    deformMaxSpin_ = new QDoubleSpinBox(valGroup);
    deformMaxSpin_->setRange(0.0, 1.0);
    deformMaxSpin_->setSingleStep(0.001);
    deformMaxSpin_->setDecimals(4);
    valForm->addRow(tr("Deform max:"), deformMaxSpin_);

    configLayout->addWidget(valGroup);

    auto* resetConfigBtn = new QPushButton(tr("Reset to live defaults"), configGroup);
    configLayout->addWidget(resetConfigBtn);
    configLayout->addStretch();

    topRow->addWidget(configGroup);
```

- [ ] **Step 5: Wire config spinbox signals at the end of `buildUi()`**

After the existing signal block (after `connect(clearBgBtn_, ...)`), add:
```cpp
    // Config spinbox → localConfig_ live update
    connect(blurSpin_,       qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v){ localConfig_.gaussian_blur_size = v; });
    connect(bgThreshSpin_,   qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v){ localConfig_.bg_subtract_threshold = v; });
    connect(morphKernelSpin_,qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v){ localConfig_.morph_kernel_size = v; });
    connect(morphIterSpin_,  qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v){ localConfig_.morph_iterations = v; });
    connect(areaMinSpin_,    qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v){ localConfig_.area_threshold_min = v; });
    connect(areaMaxSpin_,    qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v){ localConfig_.area_threshold_max = v; });
    connect(deformMinSpin_,  qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v){ localConfig_.deformability_threshold_min = v; });
    connect(deformMaxSpin_,  qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v){ localConfig_.deformability_threshold_max = v; });
    connect(resetConfigBtn,  &QPushButton::clicked,
            this, &BatchMaskDialog::resetConfigToLive);
```

- [ ] **Step 6: Implement `resetConfigToLive()`**

Add this new function anywhere after `onClearBackground()`:
```cpp
void BatchMaskDialog::resetConfigToLive() {
    localConfig_ = backend_.processing().getProcessingConfig();
    // Block valueChanged signals so they don't re-enter localConfig_ updates
    const QSignalBlocker b1(blurSpin_),       b2(bgThreshSpin_);
    const QSignalBlocker b3(morphKernelSpin_), b4(morphIterSpin_);
    const QSignalBlocker b5(areaMinSpin_),     b6(areaMaxSpin_);
    const QSignalBlocker b7(deformMinSpin_),   b8(deformMaxSpin_);
    blurSpin_->setValue(localConfig_.gaussian_blur_size);
    bgThreshSpin_->setValue(localConfig_.bg_subtract_threshold);
    morphKernelSpin_->setValue(localConfig_.morph_kernel_size);
    morphIterSpin_->setValue(localConfig_.morph_iterations);
    areaMinSpin_->setValue(localConfig_.area_threshold_min);
    areaMaxSpin_->setValue(localConfig_.area_threshold_max);
    deformMinSpin_->setValue(localConfig_.deformability_threshold_min);
    deformMaxSpin_->setValue(localConfig_.deformability_threshold_max);
}
```

- [ ] **Step 7: Update `onRun()` to use `localConfig_`**

In `onRun()`, change:
```cpp
    const auto config  = proc.getProcessingConfig();
```
to:
```cpp
    const auto config  = localConfig_;
```

---

### Task 3: Build and Verify

- [ ] **Step 1: Close the running app** (linker cannot write to a running exe)

- [ ] **Step 2: Build Debug**
```
cmake --build build --config Debug
```
Expected: zero errors. `BatchMaskDialog.cpp` and `HdfReviewTab.cpp` recompile.

- [ ] **Step 3: Launch app and open dialog**

Run `build/Debug/mib_studio_qt.exe` (or `mock_studio_qt.exe`).
Open an HDF5 file in HdfReviewTab → click "Regenerate masks…".
Confirm:
- Dialog is ~1180px wide with three visible columns
- Third column "Processing Config" shows "Image Processing" and "Validation" sub-groups
- Spinbox values match the live pipeline's current `ProcessingConfig`
- Changing blur kernel and clicking Run → log line shows correct `roi=…` and `processBatch` uses the edited values
- Clicking "Reset to live defaults" restores original values

- [ ] **Step 4: Verify live pipeline unchanged**

After closing the dialog, confirm processing in the main tab is unaffected (spinboxes only update `localConfig_`).
