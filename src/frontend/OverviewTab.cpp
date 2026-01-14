#include "frontend/OverviewTab.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
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

        // Get user-writable config directory, falling back to ../include/ for development
        static QString getUserConfigDir()
        {
            QString appDir = QCoreApplication::applicationDirPath();
            QString appDirLower = appDir.toLower();

#ifdef _WIN32
            // Check if installed in Program Files (requires admin to write)
            if (appDirLower.contains("program files") ||
                appDirLower.contains("program files (x86)"))
            {
                // Use user-writable location
                char appDataPath[MAX_PATH];
                if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appDataPath)))
                {
                    QString userConfigDir = QDir(QString::fromStdString(std::string(appDataPath) + "\\MIB_Studio_Qt\\include")).absolutePath();
                    // Ensure directory exists
                    QDir().mkpath(userConfigDir);
                    return userConfigDir;
                }
            }
#endif
            // Development: use ../include/ relative to executable
            return QDir(appDir).absoluteFilePath("../include");
        }

    } // namespace

    // Simple image canvas widget for displaying frames
    class SimpleImageCanvas : public QWidget
    {
        Q_OBJECT
    public:
        explicit SimpleImageCanvas(QImage *image, OverviewTab::FitMode *fitMode,
                                   bool *roiVisible, QPointF *roiPos, QWidget *parent = nullptr)
            : QWidget(parent), image_(image), fitMode_(fitMode),
              roiVisible_(roiVisible), roiPos_(roiPos)
        {
            setMouseTracking(true);
        }

    signals:
        void roiPositionChanged(QPointF imagePos);

    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter p(this);
            p.fillRect(rect(), Qt::black);
            if (!image_ || image_->isNull())
                return;

            const int imgW = image_->width();
            const int imgH = image_->height();

            double scale;
            QSizeF drawSize;
            QPointF topLeft;

            if (fitMode_ && *fitMode_ == OverviewTab::FitMode::Zoom100)
            {
                // 100% zoom: 1:1 pixel ratio
                scale = 1.0;
                drawSize = QSizeF(imgW, imgH);
                topLeft = QPointF((width() - drawSize.width()) / 2.0, (height() - drawSize.height()) / 2.0);
            }
            else
            {
                // Fit to window: scale to fit maintaining aspect ratio
                scale = std::min(double(width()) / imgW, double(height()) / imgH);
                drawSize = QSizeF(imgW * scale, imgH * scale);
                topLeft = QPointF((width() - drawSize.width()) / 2.0, (height() - drawSize.height()) / 2.0);
            }

            // Store transformation info for coordinate conversion
            scale_ = scale;
            topLeft_ = topLeft;
            drawSize_ = drawSize;

            // Base image
            QImage scaled = image_->scaled(drawSize.toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawImage(topLeft.toPoint(), scaled);

            // Draw ROI overlay if visible
            if (roiVisible_ && *roiVisible_ && roiPos_)
            {
                const int roiW = 512;
                const int roiH = 96;

                // Convert image coordinates to canvas coordinates
                QPointF canvasPos = imageToCanvas(*roiPos_);
                QRectF roiRect(canvasPos.x(), canvasPos.y(), roiW * scale, roiH * scale);

                // Draw semi-transparent rectangle
                p.setPen(QPen(QColor(255, 0, 0, 200), 2));
                p.setBrush(QBrush(QColor(255, 0, 0, 30)));
                p.drawRect(roiRect);
            }
        }

        void mousePressEvent(QMouseEvent *event) override
        {
            if (!roiVisible_ || !*roiVisible_ || !roiPos_ || !image_ || image_->isNull())
            {
                QWidget::mousePressEvent(event);
                return;
            }

            if (event->button() == Qt::LeftButton)
            {
                QPointF canvasPos = event->pos();
                QPointF imagePos = canvasToImage(canvasPos);

                // Check if click is within ROI rectangle
                const int roiW = 512;
                const int roiH = 96;
                QRectF roiRect(roiPos_->x(), roiPos_->y(), roiW, roiH);

                if (roiRect.contains(imagePos))
                {
                    dragging_ = true;
                    dragStartCanvasPos_ = canvasPos;
                    dragStartRoiPos_ = *roiPos_;
                }
            }
        }

        void mouseMoveEvent(QMouseEvent *event) override
        {
            if (dragging_ && roiPos_)
            {
                QPointF canvasPos = event->pos();
                QPointF deltaCanvas = canvasPos - dragStartCanvasPos_;
                QPointF deltaImage = QPointF(deltaCanvas.x() / scale_, deltaCanvas.y() / scale_);

                QPointF newRoiPos = dragStartRoiPos_ + deltaImage;

                // Constrain to image bounds
                if (!image_ || image_->isNull())
                {
                    QWidget::mouseMoveEvent(event);
                    return;
                }

                const int imgW = image_->width();
                const int imgH = image_->height();
                const int roiW = 512;
                const int roiH = 96;

                // Constrain to image bounds first
                newRoiPos.setX(std::max(0.0, std::min(double(imgW - roiW), newRoiPos.x())));
                newRoiPos.setY(std::max(0.0, std::min(double(imgH - roiH), newRoiPos.y())));

                // Snap to alignment constraints (X step=4, Y step=16)
                int maxOffsetX = imgW - roiW;
                int maxOffsetY = imgH - roiH;
                int snappedX = snapToStep(static_cast<int>(std::round(newRoiPos.x())), ROI_OFFSET_X_STEP, maxOffsetX);
                int snappedY = snapToStep(static_cast<int>(std::round(newRoiPos.y())), ROI_OFFSET_Y_STEP, maxOffsetY);

                *roiPos_ = QPointF(snappedX, snappedY);
                update();
            }
        }

        void mouseReleaseEvent(QMouseEvent *event) override
        {
            if (dragging_ && event->button() == Qt::LeftButton)
            {
                dragging_ = false;
                if (roiPos_)
                {
                    emit roiPositionChanged(*roiPos_);
                }
            }
            QWidget::mouseReleaseEvent(event);
        }

    private:
        QPointF canvasToImage(const QPointF &canvasPos) const
        {
            QPointF relative = canvasPos - topLeft_;
            return QPointF(relative.x() / scale_, relative.y() / scale_);
        }

        QPointF imageToCanvas(const QPointF &imagePos) const
        {
            return topLeft_ + QPointF(imagePos.x() * scale_, imagePos.y() * scale_);
        }

        QImage *image_ = nullptr;
        OverviewTab::FitMode *fitMode_ = nullptr;
        bool *roiVisible_ = nullptr;
        QPointF *roiPos_ = nullptr;

        // Transformation state
        double scale_ = 1.0;
        QPointF topLeft_;
        QSizeF drawSize_;

        // Dragging state
        bool dragging_ = false;
        QPointF dragStartCanvasPos_;
        QPointF dragStartRoiPos_;
    };

    namespace
    {
        static bool ensureDefaultsFile(const QString &targetPath, const QString &resourceName, QString *err)
        {
            QFileInfo fi(targetPath);
            QDir dir(fi.absolutePath());
            if (!dir.exists())
            {
                if (!dir.mkpath("."))
                {
                    if (err)
                        *err = QObject::tr("Failed to create directory: %1").arg(dir.absolutePath());
                    return false;
                }
            }
            if (QFile::exists(targetPath))
            {
                return true;
            }
            QFile res(resourceName);
            if (!res.open(QIODevice::ReadOnly))
            {
                if (err)
                    *err = QObject::tr("Failed to open resource: %1").arg(resourceName);
                return false;
            }
            QFile out(targetPath);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                if (err)
                    *err = QObject::tr("Failed to create: %1").arg(targetPath);
                return false;
            }
            const QByteArray data = res.readAll();
            if (out.write(data) != data.size())
            {
                if (err)
                    *err = QObject::tr("Failed to write: %1").arg(targetPath);
                return false;
            }
            return true;
        }

    } // namespace

    OverviewTab::OverviewTab(backend::AppBackend &backend, QWidget *parent)
        : QWidget(parent), backend_(backend)
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // Create vertical splitter for resizable top/bottom sections
        QSplitter *splitter = new QSplitter(Qt::Vertical, this);
        splitter->setChildrenCollapsible(false);
        splitter->setHandleWidth(10);
        splitter->setOpaqueResize(true);

        // Top: Frame display with controls
        QWidget *canvasContainer = new QWidget(this);
        canvasContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        canvasContainer->setMinimumHeight(100);
        auto *canvasLayout = new QVBoxLayout(canvasContainer);
        canvasLayout->setContentsMargins(0, 0, 0, 0);
        canvasLayout->setSpacing(0);

        // Controls bar
        QWidget *controls = new QWidget(canvasContainer);
        auto *controlsLayout = new QHBoxLayout(controls);
        controlsLayout->setContentsMargins(6, 4, 6, 4);
        controlsLayout->setSpacing(6);
        fitBtn_ = new QToolButton(controls);
        fitBtn_->setText(tr("Fit: Window"));
        fitBtn_->setToolTip(tr("Toggle between fit-to-window and 100% zoom"));
        controlsLayout->addWidget(fitBtn_);

        roiOverlayBtn_ = new QToolButton(controls);
        roiOverlayBtn_->setText(tr("ROI Overlay: Off"));
        roiOverlayBtn_->setToolTip(tr("Toggle ROI overlay (512x96 rectangle)"));
        controlsLayout->addWidget(roiOverlayBtn_);

        controlsLayout->addStretch(1);
        canvasLayout->addWidget(controls);

        canvas_ = new SimpleImageCanvas(&frameImage_, &fitMode_, &roiOverlayVisible_, &roiPosition_, canvasContainer);
        canvas_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        canvasLayout->addWidget(canvas_, 1);

        splitter->addWidget(canvasContainer);

        // Bottom: Camera script configuration
        QWidget *configWidget = new QWidget(this);
        configWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        configWidget->setMinimumHeight(0);
        auto *configLayout = new QVBoxLayout(configWidget);
        configLayout->setContentsMargins(6, 6, 6, 6);
        configLayout->setSpacing(6);

        // Camera script editor
        jsEdit_ = new QPlainTextEdit(configWidget);
        jsEdit_->setWordWrapMode(QTextOption::NoWrap);

        // Buttons row
        auto *buttonRow = new QHBoxLayout();
        jsReloadBtn_ = new QPushButton(tr("Reset"), configWidget);
        jsSaveBtn_ = new QPushButton(tr("Save"), configWidget);
        jsApplyBtn_ = new QPushButton(tr("Apply to Camera"), configWidget);
        jsBrowseBtn_ = new QPushButton(tr("Browse..."), configWidget);
        jsClearBtn_ = new QPushButton(tr("Clear"), configWidget);
        jsPathLabel_ = new QLabel(configWidget);
        jsPathLabel_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        jsPathLabel_->setTextFormat(Qt::PlainText);
        jsPathLabel_->setWordWrap(false);
        jsPathLabel_->setMinimumWidth(0);
        jsPathLabel_->setMaximumWidth(400);
        jsUnsavedLabel_ = new QLabel(configWidget);
        jsUnsavedLabel_->setText(tr("Unsaved changes – click Save to apply."));
        jsUnsavedLabel_->setVisible(false);
        jsUnsavedLabel_->setStyleSheet("color: #d17a00;");
        jsUnsavedLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

        buttonRow->addWidget(jsReloadBtn_);
        buttonRow->addWidget(jsSaveBtn_);
        buttonRow->addWidget(jsApplyBtn_);
        buttonRow->addWidget(jsBrowseBtn_);
        buttonRow->addWidget(jsClearBtn_);
        buttonRow->addStretch(1);
        buttonRow->addWidget(jsPathLabel_);
        buttonRow->addSpacing(8);
        buttonRow->addWidget(jsUnsavedLabel_);

        configLayout->addLayout(buttonRow);
        configLayout->addWidget(jsEdit_, 1);

        splitter->addWidget(configWidget);

        // Set initial proportions (3:2 ratio)
        splitter->setStretchFactor(0, 3);
        splitter->setStretchFactor(1, 2);

        root->addWidget(splitter);

        // Connect button signals
        connect(jsReloadBtn_, &QPushButton::clicked, this, &OverviewTab::onReloadJs);
        connect(jsSaveBtn_, &QPushButton::clicked, this, &OverviewTab::onSaveJs);
        connect(jsApplyBtn_, &QPushButton::clicked, this, &OverviewTab::onApplyJs);
        connect(jsBrowseBtn_, &QPushButton::clicked, this, &OverviewTab::onBrowseJs);
        connect(jsClearBtn_, &QPushButton::clicked, this, &OverviewTab::onClearJs);
        connect(fitBtn_, &QToolButton::clicked, this, &OverviewTab::onToggleFit);
        connect(roiOverlayBtn_, &QToolButton::clicked, this, &OverviewTab::onToggleRoiOverlay);
        connect(static_cast<SimpleImageCanvas *>(canvas_), &SimpleImageCanvas::roiPositionChanged,
                this, &OverviewTab::onRoiPositionChanged);
        connect(jsEdit_, &QPlainTextEdit::textChanged, this, [this]()
                {
        if (jsUnsavedLabel_) jsUnsavedLabel_->setVisible(true); });

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

    QString OverviewTab::appDirIncludePath(const QString &fileName) const
    {
        return QDir(getUserConfigDir()).absoluteFilePath(fileName);
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
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (err)
                *err = f.errorString();
            return false;
        }
        QTextStream in(&f);
        const bool blocked = editor->blockSignals(true);
        editor->setPlainText(in.readAll());
        editor->blockSignals(blocked);
        if (editor == jsEdit_ && jsUnsavedLabel_)
            jsUnsavedLabel_->setVisible(false);
        return true;
    }

    bool OverviewTab::saveEditorToFile(QPlainTextEdit *editor, const QString &path, QString *err)
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        {
            if (err)
                *err = f.errorString();
            return false;
        }
        QTextStream out(&f);
        out << editor->toPlainText();
        return true;
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
            if (!ensureDefaultsFile(path, ":/defaults/overviewConfig.js", &err))
            {
                SPDLOG_WARN("ensureDefaultsFile(overviewConfig.js) failed: {}", err.toStdString());
            }
        }
        QString err;
        if (!loadFileToEditor(path, jsEdit_, &err))
        {
            SPDLOG_WARN("Failed to load overviewConfig.js from {}: {}", path.toStdString(), err.toStdString());
            QMessageBox::warning(this, tr("Reset overviewConfig.js"), tr("Failed to load: %1").arg(err));
            return;
        }
        jsPathLabel_->setText(path);
        if (jsUnsavedLabel_)
            jsUnsavedLabel_->setVisible(false);
    }

    void OverviewTab::onSaveJs()
    {
        const QString path = currentJsPath();
        QString err;
        if (!saveEditorToFile(jsEdit_, path, &err))
        {
            SPDLOG_ERROR("Failed to save overviewConfig.js to {}: {}", path.toStdString(), err.toStdString());
            QMessageBox::warning(this, tr("Save overviewConfig.js"), tr("Failed to save: %1").arg(err));
            return;
        }
        QMessageBox::information(this, tr("Save overviewConfig.js"), tr("Saved."));
        if (jsUnsavedLabel_)
            jsUnsavedLabel_->setVisible(false);
    }

    void OverviewTab::onApplyJs()
    {
        const QString path = currentJsPath();
        QString err;
        // Always save first to ensure the latest content is applied
        {
            QString saveErr;
            if (!saveEditorToFile(jsEdit_, path, &saveErr))
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
        if (!loadFileToEditor(selected, jsEdit_, &err))
        {
            SPDLOG_WARN("Failed to load external overviewConfig.js from {}: {}", selected.toStdString(), err.toStdString());
            QMessageBox::warning(this, tr("Reset overviewConfig.js"), tr("Failed to load: %1").arg(err));
            return;
        }
        jsPathLabel_->setText(selected);
        if (jsUnsavedLabel_)
            jsUnsavedLabel_->setVisible(false);
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
            if (fitBtn_)
            {
                fitBtn_->setText(tr("Fit: 100%"));
                fitBtn_->setToolTip(tr("Toggle between fit-to-window and 100% zoom"));
            }
        }
        else
        {
            fitMode_ = FitMode::FitToWindow;
            if (fitBtn_)
            {
                fitBtn_->setText(tr("Fit: Window"));
                fitBtn_->setToolTip(tr("Toggle between fit-to-window and 100% zoom"));
            }
        }
        if (canvas_)
            canvas_->update();
    }

    void OverviewTab::onToggleRoiOverlay()
    {
        roiOverlayVisible_ = !roiOverlayVisible_;
        if (roiOverlayBtn_)
        {
            if (roiOverlayVisible_)
            {
                roiOverlayBtn_->setText(tr("ROI Overlay: On"));
            }
            else
            {
                roiOverlayBtn_->setText(tr("ROI Overlay: Off"));
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
            if (!ensureDefaultsFile(path, ":/defaults/egrabberConfig.js", &err))
            {
                SPDLOG_WARN("ensureDefaultsFile(egrabberConfig.js) failed: {}", err.toStdString());
            }
        }

        QFile file(path);

        if (!file.exists())
        {
            SPDLOG_WARN("egrabberConfig.js not found at {}", path.toStdString());
            return;
        }

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            SPDLOG_ERROR("Failed to open egrabberConfig.js for reading: {}", path.toStdString());
            return;
        }

        QTextStream in(&file);
        QStringList lines = in.readAll().split('\n');
        file.close();

        // Update lines 16-17 (0-indexed: lines 15-16)
        // Line 16: g.RemotePort.set("OffsetY", <value>);
        // Line 17: g.RemotePort.set("OffsetX", <value>);
        int requestedY = static_cast<int>(std::round(imagePos.y()));
        int requestedX = static_cast<int>(std::round(imagePos.x()));

        // Snap to alignment constraints (X step=4, Y step=16)
        // For maximum bounds, we need to know image size - use reasonable defaults if unavailable
        // The actual bounds will be enforced by the camera, but we snap to valid increments
        const int maxOffsetX = 10000; // Large enough for typical cameras
        const int maxOffsetY = 10000;
        int offsetY = snapToStep(requestedY, ROI_OFFSET_Y_STEP, maxOffsetY);
        int offsetX = snapToStep(requestedX, ROI_OFFSET_X_STEP, maxOffsetX);

        // Match only non-commented lines (lines that don't start with // or have // before the pattern)
        QRegularExpression reOffsetY(R"(^\s*g\.RemotePort\.set\("OffsetY",\s*(\d+)\);)");
        QRegularExpression reOffsetX(R"(^\s*g\.RemotePort\.set\("OffsetX",\s*(\d+)\);)");

        // Search through all lines to find and update OffsetY and OffsetX (only non-commented lines)
        for (int i = 0; i < lines.size(); ++i)
        {
            QString line = lines[i];
            // Skip commented lines
            if (line.trimmed().startsWith("//"))
            {
                continue;
            }

            QRegularExpressionMatch matchY = reOffsetY.match(line);
            if (matchY.hasMatch())
            {
                lines[i] = QString("g.RemotePort.set(\"OffsetY\", %1);").arg(offsetY);
            }

            QRegularExpressionMatch matchX = reOffsetX.match(line);
            if (matchX.hasMatch())
            {
                lines[i] = QString("g.RemotePort.set(\"OffsetX\", %1);").arg(offsetX);
            }
        }

        // Write back to file
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        {
            SPDLOG_ERROR("Failed to open egrabberConfig.js for writing: {}", path.toStdString());
            return;
        }

        QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8");
#endif
        for (int i = 0; i < lines.size(); ++i)
        {
            out << lines[i];
            if (i < lines.size() - 1)
                out << '\n';
        }
        file.close();

        if (requestedX != offsetX || requestedY != offsetY)
        {
            SPDLOG_INFO("Updated egrabberConfig.js at {}: requested=({},{}) snapped=({},{})",
                        path.toStdString(), requestedX, requestedY, offsetX, offsetY);
        }
        else
        {
            SPDLOG_INFO("Updated egrabberConfig.js at {}: OffsetX={}, OffsetY={}", path.toStdString(), offsetX, offsetY);
        }
    }

    void OverviewTab::initializeRoiFromConfig()
    {
        const QString path = egrabberConfigPath();
        QFile file(path);

        if (!file.exists())
        {
            // Default position: center of a typical 1920x1080 image
            roiPosition_ = QPointF(704, 500);
            return;
        }

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            SPDLOG_WARN("Failed to read egrabberConfig.js for ROI initialization: {}", path.toStdString());
            roiPosition_ = QPointF(704, 500);
            return;
        }

        QTextStream in(&file);
        QString content = in.readAll();
        file.close();

        // Extract OffsetY and OffsetX from content (only non-commented lines)
        QRegularExpression reOffsetY(R"(^\s*g\.RemotePort\.set\("OffsetY",\s*(\d+)\);)", QRegularExpression::MultilineOption);
        QRegularExpression reOffsetX(R"(^\s*g\.RemotePort\.set\("OffsetX",\s*(\d+)\);)", QRegularExpression::MultilineOption);

        // Split content into lines and find first non-commented match
        QStringList contentLines = content.split('\n');
        QRegularExpressionMatch matchY;
        QRegularExpressionMatch matchX;

        for (const QString &line : contentLines)
        {
            // Skip commented lines
            if (line.trimmed().startsWith("//"))
            {
                continue;
            }

            QRegularExpressionMatch mY = reOffsetY.match(line);
            if (mY.hasMatch() && !matchY.hasMatch())
            {
                matchY = mY;
            }

            QRegularExpressionMatch mX = reOffsetX.match(line);
            if (mX.hasMatch() && !matchX.hasMatch())
            {
                matchX = mX;
            }
        }

        int offsetY = 500; // default
        int offsetX = 704; // default

        if (matchY.hasMatch())
        {
            offsetY = matchY.captured(1).toInt();
        }

        if (matchX.hasMatch())
        {
            offsetX = matchX.captured(1).toInt();
        }

        // Snap to alignment constraints if needed
        int requestedX = offsetX;
        int requestedY = offsetY;
        const int maxOffsetX = 10000; // Large enough for typical cameras
        const int maxOffsetY = 10000;
        int snappedX = snapToStep(offsetX, ROI_OFFSET_X_STEP, maxOffsetX);
        int snappedY = snapToStep(offsetY, ROI_OFFSET_Y_STEP, maxOffsetY);

        // If offsets were invalid, rewrite the file with snapped values
        if (snappedX != offsetX || snappedY != offsetY)
        {
            SPDLOG_WARN("egrabberConfig.js contained invalid ROI offsets: requested=({},{}) snapped=({},{})",
                        requestedX, requestedY, snappedX, snappedY);

            // Update the file with snapped values
            QStringList lines = content.split('\n');
            QRegularExpression reOffsetY(R"(^\s*g\.RemotePort\.set\("OffsetY",\s*(\d+)\);)");
            QRegularExpression reOffsetX(R"(^\s*g\.RemotePort\.set\("OffsetX",\s*(\d+)\);)");

            for (int i = 0; i < lines.size(); ++i)
            {
                QString line = lines[i];
                if (line.trimmed().startsWith("//"))
                {
                    continue;
                }

                QRegularExpressionMatch matchY = reOffsetY.match(line);
                if (matchY.hasMatch())
                {
                    lines[i] = QString("g.RemotePort.set(\"OffsetY\", %1);").arg(snappedY);
                }

                QRegularExpressionMatch matchX = reOffsetX.match(line);
                if (matchX.hasMatch())
                {
                    lines[i] = QString("g.RemotePort.set(\"OffsetX\", %1);").arg(snappedX);
                }
            }

            // Write back to file
            QFile writeFile(path);
            if (writeFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
            {
                QTextStream out(&writeFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                out.setCodec("UTF-8");
#endif
                for (int i = 0; i < lines.size(); ++i)
                {
                    out << lines[i];
                    if (i < lines.size() - 1)
                        out << '\n';
                }
                writeFile.close();
                SPDLOG_INFO("Rewrote egrabberConfig.js with valid ROI offsets: OffsetX={}, OffsetY={}", snappedX, snappedY);
            }
            else
            {
                SPDLOG_WARN("Failed to rewrite egrabberConfig.js with valid offsets");
            }
        }

        roiPosition_ = QPointF(snappedX, snappedY);
        SPDLOG_DEBUG("Initialized ROI position from egrabberConfig.js: OffsetX={}, OffsetY={}", snappedX, snappedY);
    }

} // namespace frontend

// Include moc file for SimpleImageCanvas class (defined in this .cpp file with Q_OBJECT)
#include "OverviewTab.moc"
