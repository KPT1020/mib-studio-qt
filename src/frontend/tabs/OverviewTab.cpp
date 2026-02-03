#include "frontend/tabs/OverviewTab.h"
#include "ui_OverviewTab.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPainter>
#include <QTextStream>
#include <QTimer>
#include <QSettings>
#include <QTextOption>
#include <QMouseEvent>
#include <QRegularExpression>

#include <spdlog/spdlog.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#endif

#include "backend/AppBackend.h"
#include "backend/services/PlaybackService.h"
#include "backend/playback/FrameStore.h"
#include "frontend/utils/ConfigPathManager.h"
#include "frontend/utils/FileIOUtils.h"
#include "frontend/utils/SimpleImageCanvas.h"
#include "frontend/utils/EgrabberConfigParser.h"

namespace frontend
{

    namespace
    {

        // ROI alignment constraints: OffsetX must be multiple of 4, OffsetY must be multiple of 16
        static constexpr int ROI_OFFSET_X_STEP = 16;
        static constexpr int ROI_OFFSET_Y_STEP = 4;

        // Snap a value to the nearest multiple of step, clamping to [0, max]
        static int snapToStep(int value, int step, int max)
        {
            int snapped = (value / step) * step;
            if (snapped > max)
                snapped = (max / step) * step; // Clamp to max aligned value
            if (snapped < 0)
                snapped = 0;
            return snapped;
        }

    } // namespace

    OverviewTab::OverviewTab(backend::AppBackend &backend, QWidget *parent)
        : QWidget(parent), ui(new Ui::OverviewTab), backend_(backend)
    {
        ui->setupUi(this);

        // Create custom SimpleImageCanvas widget and add it to the placeholder
        canvas_ = new SimpleImageCanvas(&frameImage_, &fitMode_, &roiOverlayVisible_, &roiPosition_, ui->canvasContainer);
        canvas_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        // Replace the placeholder with the actual canvas
        ui->canvasLayout->removeWidget(ui->canvasPlaceholder);
        delete ui->canvasPlaceholder;
        ui->canvasLayout->addWidget(canvas_, 1);

        // Set initial proportions (50/50 ratio)
        ui->splitter->setStretchFactor(0, 1);
        ui->splitter->setStretchFactor(1, 1);
        
        // Set explicit sizes for 50/50 split after widget is shown
        QTimer::singleShot(0, this, [this]() {
            int height = ui->splitter->height();
            if (height > 0) {
                int half = height / 2;
                ui->splitter->setSizes({half, half});
            }
        });

        // Connect button signals
        connect(ui->jsReloadBtn, &QPushButton::clicked, this, &OverviewTab::onReloadJs);
        connect(ui->jsSaveBtn, &QPushButton::clicked, this, &OverviewTab::onSaveJs);
        connect(ui->jsApplyBtn, &QPushButton::clicked, this, &OverviewTab::onApplyJs);
        connect(ui->jsBrowseBtn, &QPushButton::clicked, this, &OverviewTab::onBrowseJs);
        connect(ui->jsClearBtn, &QPushButton::clicked, this, &OverviewTab::onClearJs);
        connect(ui->fitBtn, &QToolButton::clicked, this, &OverviewTab::onToggleFit);
        connect(ui->roiOverlayBtn, &QToolButton::clicked, this, &OverviewTab::onToggleRoiOverlay);
        connect(static_cast<SimpleImageCanvas*>(canvas_), &SimpleImageCanvas::roiPositionChanged,
                this, &OverviewTab::onRoiPositionChanged);
        connect(ui->jsEdit, &QPlainTextEdit::textChanged, this, [this]()
                {
        if (ui->jsUnsavedLabel) ui->jsUnsavedLabel->setVisible(true); });

        // Timer for frame display at 50 fps (20ms interval)
        timer_ = new QTimer(this);
        timer_->setTimerType(Qt::PreciseTimer);
        timer_->setInterval(20); // 50 fps = 1000ms / 50 = 20ms
        connect(timer_, &QTimer::timeout, this, &OverviewTab::onTick);
        timer_->start();
        SPDLOG_INFO("Overview tab: display_fps=50 (~20 ms)");

        // Load initial camera script
        onReloadJs();

        // Initialize ROI position from egrabberConfig.js
        initializeRoiFromConfig();
    }

    OverviewTab::~OverviewTab() {
        delete ui;
    }

    QString OverviewTab::appDirIncludePath(const QString &fileName) const
    {
        return ConfigPathManager::getIncludePath(fileName);
    }

    QString OverviewTab::currentJsPath() const
    {
        QSettings s;
        const QString ext = s.value("Config/ExternalOverviewScriptPath").toString().trimmed();
        if (!ext.isEmpty())
            return ext;
        return defaultJsPath();
    }

    bool OverviewTab::loadFileToEditor(const QString &path, QPlainTextEdit *editor, QString *err)
    {
        bool result = FileIOUtils::loadFileToEditor(path, editor, err);
        if (result && editor == ui->jsEdit && ui->jsUnsavedLabel)
            ui->jsUnsavedLabel->setVisible(false);
        return result;
    }

    bool OverviewTab::saveEditorToFile(QPlainTextEdit *editor, const QString &path, QString *err)
    {
        return FileIOUtils::saveEditorToFile(editor, path, err);
    }

    void OverviewTab::setRoiOverlayVisible(bool visible)
    {
        roiOverlayVisible_ = visible;
        if (ui->roiOverlayBtn)
        {
            ui->roiOverlayBtn->setText(roiOverlayVisible_ ? tr("ROI Overlay: On") : tr("ROI Overlay: Off"));
        }
        if (canvas_)
        {
            canvas_->update();
        }
    }

    void OverviewTab::onTick()
    {
        // Fetch latest frame from playback service
        backend::playback::Frame f;
        bool got = backend_.playback().fetchLatest(f);

        if (got)
        {
            // Convert Mono8 to QImage; fallback to grayscale if unknown
            if (f.pixelFormat == 0x01080001 /* PFNC Mono8 */ || true)
            {
                const int w = static_cast<int>(f.width);
                const int h = static_cast<int>(f.height);
                const int pitch = static_cast<int>(f.linePitch == 0 ? f.width : f.linePitch);
                QImage img(f.data.data(), w, h, pitch, QImage::Format_Grayscale8);
                frameImage_ = img.copy(); // ensure ownership
            }
            else
            {
                frameImage_ = QImage();
            }
            if (canvas_)
                canvas_->update();
        }
    }

    void OverviewTab::onReloadJs()
    {
        const QString path = currentJsPath();
        if (path == defaultJsPath())
        {
            QString err;
            if (!FileIOUtils::ensureDefaultsFile(path, ":/defaults/overviewConfig.js", &err))
            {
                SPDLOG_WARN("ensureDefaultsFile(overviewConfig.js) failed: {}", err.toStdString());
            }
        }
        QString err;
        if (!loadFileToEditor(path, ui->jsEdit, &err))
        {
            SPDLOG_WARN("Failed to load overviewConfig.js from {}: {}", path.toStdString(), err.toStdString());
            QMessageBox::warning(this, tr("Reset overviewConfig.js"), tr("Failed to load: %1").arg(err));
            return;
        }
        ui->jsPathLabel->setText(path);
        if (ui->jsUnsavedLabel)
            ui->jsUnsavedLabel->setVisible(false);
    }

    void OverviewTab::onSaveJs()
    {
        const QString path = currentJsPath();
        QString err;
        if (!saveEditorToFile(ui->jsEdit, path, &err))
        {
            SPDLOG_ERROR("Failed to save overviewConfig.js to {}: {}", path.toStdString(), err.toStdString());
            QMessageBox::warning(this, tr("Save overviewConfig.js"), tr("Failed to save: %1").arg(err));
            return;
        }
        QMessageBox::information(this, tr("Save overviewConfig.js"), tr("Saved."));
        if (ui->jsUnsavedLabel)
            ui->jsUnsavedLabel->setVisible(false);
    }

    void OverviewTab::onApplyJs()
    {
        const QString path = currentJsPath();
        QString err;
        // Always save first to ensure the latest content is applied
        {
            QString saveErr;
            if (!saveEditorToFile(ui->jsEdit, path, &saveErr))
            {
                QMessageBox::warning(this, tr("Apply Camera Script"), tr("Failed to save script: %1").arg(saveErr));
                return;
            }
        }

        std::string backendErr;
        if (!backend_.applyCameraScriptFromFile(path.toStdString(), &backendErr))
        {
            QMessageBox::warning(this,
                                 tr("Apply Camera Script"),
                                 tr("Failed to apply script: %1").arg(QString::fromStdString(backendErr)));
            return;
        }
        QMessageBox::information(this, tr("Apply Camera Script"), tr("Applied to camera. Capture remains stopped."));
    }

    void OverviewTab::onBrowseJs()
    {
        const QString current = currentJsPath();
        const QString initialDir = QFileInfo(current).absolutePath();
        const QString selected = QFileDialog::getOpenFileName(this,
                                                              tr("Select Camera script (overviewConfig.js)"),
                                                              initialDir,
                                                              tr("JavaScript files (*.js);;All Files (*.*)"));
        if (selected.isEmpty())
            return;
        {
            QSettings s;
            s.setValue("Config/ExternalOverviewScriptPath", selected);
        }
        SPDLOG_INFO("External Overview script set to {}", selected.toStdString());
        QString err;
        if (!loadFileToEditor(selected, ui->jsEdit, &err))
        {
            SPDLOG_WARN("Failed to load external overviewConfig.js from {}: {}", selected.toStdString(), err.toStdString());
            QMessageBox::warning(this, tr("Reset overviewConfig.js"), tr("Failed to load: %1").arg(err));
            return;
        }
        ui->jsPathLabel->setText(selected);
        if (ui->jsUnsavedLabel)
            ui->jsUnsavedLabel->setVisible(false);
    }

    void OverviewTab::onClearJs()
    {
        QSettings s;
        s.remove("Config/ExternalOverviewScriptPath");
        SPDLOG_INFO("External Overview script cleared; reverting to default include path");
        const auto ret = QMessageBox::question(this,
                                               tr("Camera Script Path Cleared"),
                                               tr("External Camera script path cleared.\nReset from default include path now?\n\nNote: Save to apply any changes."),
                                               QMessageBox::Yes | QMessageBox::No,
                                               QMessageBox::Yes);
        if (ret == QMessageBox::Yes)
        {
            onReloadJs();
        }
    }

    void OverviewTab::onToggleFit()
    {
        if (fitMode_ == FitMode::FitToWindow)
        {
            fitMode_ = FitMode::Zoom100;
            if (ui->fitBtn)
            {
                ui->fitBtn->setText(tr("Fit: 100%"));
                ui->fitBtn->setToolTip(tr("Toggle between fit-to-window and 100% zoom"));
            }
        }
        else
        {
            fitMode_ = FitMode::FitToWindow;
            if (ui->fitBtn)
            {
                ui->fitBtn->setText(tr("Fit: Window"));
                ui->fitBtn->setToolTip(tr("Toggle between fit-to-window and 100% zoom"));
            }
        }
        if (canvas_)
            canvas_->update();
    }

    void OverviewTab::onToggleRoiOverlay()
    {
        roiOverlayVisible_ = !roiOverlayVisible_;
        if (ui->roiOverlayBtn)
        {
            if (roiOverlayVisible_)
            {
                ui->roiOverlayBtn->setText(tr("ROI Overlay: On"));
            }
            else
            {
                ui->roiOverlayBtn->setText(tr("ROI Overlay: Off"));
            }
        }
        if (canvas_)
            canvas_->update();
    }

    void OverviewTab::onRoiPositionChanged(QPointF imagePos)
    {
        roiPosition_ = imagePos;
        updateEgrabberConfigFromRect(imagePos);
    }

    QString OverviewTab::egrabberConfigPath() const
    {
        QSettings s;
        const QString ext = s.value("Config/ExternalCameraScriptPath").toString().trimmed();
        if (!ext.isEmpty())
            return ext;
        return appDirIncludePath("egrabberConfig.js");
    }

    void OverviewTab::updateEgrabberConfigFromRect(QPointF imagePos)
    {
        const QString path = egrabberConfigPath();

        // Ensure default file exists if using default path
        if (path == appDirIncludePath("egrabberConfig.js"))
        {
            QString err;
            if (!FileIOUtils::ensureDefaultsFile(path, ":/defaults/egrabberConfig.js", &err))
            {
                SPDLOG_WARN("ensureDefaultsFile(egrabberConfig.js) failed: {}", err.toStdString());
            }
        }

        int requestedX = static_cast<int>(std::round(imagePos.x()));
        int requestedY = static_cast<int>(std::round(imagePos.y()));

        QString err;
        if (!EgrabberConfigParser::updateRoiOffsets(path, requestedX, requestedY, &err))
        {
            SPDLOG_ERROR("Failed to update egrabberConfig.js: {}", err.toStdString());
        }
    }

    void OverviewTab::initializeRoiFromConfig()
    {
        const QString path = egrabberConfigPath();
        int offsetX, offsetY;
        if (EgrabberConfigParser::readRoiOffsets(path, offsetX, offsetY))
        {
            roiPosition_ = QPointF(offsetX, offsetY);
            SPDLOG_DEBUG("Initialized ROI position from egrabberConfig.js: OffsetX={}, OffsetY={}", offsetX, offsetY);
        }
        else
        {
            // Use default position
            QPoint defaultPos = EgrabberConfigParser::defaultRoiPosition();
            roiPosition_ = QPointF(defaultPos);
        }
    }

} // namespace frontend
