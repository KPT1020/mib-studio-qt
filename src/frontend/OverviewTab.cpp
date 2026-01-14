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

namespace {

// Get user-writable config directory, falling back to ../include/ for development
static QString getUserConfigDir() {
    QString appDir = QCoreApplication::applicationDirPath();
    QString appDirLower = appDir.toLower();
    
#ifdef _WIN32
    // Check if installed in Program Files (requires admin to write)
    if (appDirLower.contains("program files") || 
        appDirLower.contains("program files (x86)")) {
        // Use user-writable location
        char appDataPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appDataPath))) {
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

// Simple image canvas widget for displaying frames
class SimpleImageCanvas : public QWidget
{
public:
    explicit SimpleImageCanvas(QImage* image, OverviewTab::FitMode* fitMode, QWidget* parent = nullptr)
        : QWidget(parent), image_(image), fitMode_(fitMode) {}

protected:
    void paintEvent(QPaintEvent*) override
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

        // Base image
        QImage scaled = image_->scaled(drawSize.toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage(topLeft.toPoint(), scaled);
    }

private:
    QImage* image_ = nullptr;
    OverviewTab::FitMode* fitMode_ = nullptr;
};

static bool ensureDefaultsFile(const QString& targetPath, const QString& resourceName, QString* err) {
    QFileInfo fi(targetPath);
    QDir dir(fi.absolutePath());
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            if (err) *err = QObject::tr("Failed to create directory: %1").arg(dir.absolutePath());
            return false;
        }
    }
    if (QFile::exists(targetPath)) {
        return true;
    }
    QFile res(resourceName);
    if (!res.open(QIODevice::ReadOnly)) {
        if (err) *err = QObject::tr("Failed to open resource: %1").arg(resourceName);
        return false;
    }
    QFile out(targetPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = QObject::tr("Failed to create: %1").arg(targetPath);
        return false;
    }
    const QByteArray data = res.readAll();
    if (out.write(data) != data.size()) {
        if (err) *err = QObject::tr("Failed to write: %1").arg(targetPath);
        return false;
    }
    return true;
}

} // namespace

OverviewTab::OverviewTab(backend::AppBackend& backend, QWidget* parent)
    : QWidget(parent), backend_(backend)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Create vertical splitter for resizable top/bottom sections
    QSplitter* splitter = new QSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(10);
    splitter->setOpaqueResize(true);

    // Top: Frame display with controls
    QWidget* canvasContainer = new QWidget(this);
    canvasContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    canvasContainer->setMinimumHeight(100);
    auto* canvasLayout = new QVBoxLayout(canvasContainer);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(0);

    // Controls bar
    QWidget* controls = new QWidget(canvasContainer);
    auto* controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(6, 4, 6, 4);
    controlsLayout->setSpacing(6);
    fitBtn_ = new QToolButton(controls);
    fitBtn_->setText(tr("Fit: Window"));
    fitBtn_->setToolTip(tr("Toggle between fit-to-window and 100% zoom"));
    controlsLayout->addWidget(fitBtn_);
    controlsLayout->addStretch(1);
    canvasLayout->addWidget(controls);

    canvas_ = new SimpleImageCanvas(&frameImage_, &fitMode_, canvasContainer);
    canvas_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    canvasLayout->addWidget(canvas_, 1);

    splitter->addWidget(canvasContainer);

    // Bottom: Camera script configuration
    QWidget* configWidget = new QWidget(this);
    configWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    configWidget->setMinimumHeight(0);
    auto* configLayout = new QVBoxLayout(configWidget);
    configLayout->setContentsMargins(6, 6, 6, 6);
    configLayout->setSpacing(6);

    // Camera script editor
    jsEdit_ = new QPlainTextEdit(configWidget);
    jsEdit_->setWordWrapMode(QTextOption::NoWrap);
    
    // Buttons row
    auto* buttonRow = new QHBoxLayout();
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
    connect(jsEdit_, &QPlainTextEdit::textChanged, this, [this]() {
        if (jsUnsavedLabel_) jsUnsavedLabel_->setVisible(true);
    });

    // Timer for frame display at 50 fps (20ms interval)
    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    timer_->setInterval(20); // 50 fps = 1000ms / 50 = 20ms
    connect(timer_, &QTimer::timeout, this, &OverviewTab::onTick);
    timer_->start();
    SPDLOG_INFO("Overview tab: display_fps=50 (~20 ms)");

    // Load initial camera script
    onReloadJs();
}

QString OverviewTab::appDirIncludePath(const QString& fileName) const {
    return QDir(getUserConfigDir()).absoluteFilePath(fileName);
}

QString OverviewTab::currentJsPath() const {
    QSettings s;
    const QString ext = s.value("Config/ExternalOverviewScriptPath").toString().trimmed();
    if (!ext.isEmpty()) return ext;
    return defaultJsPath();
}

bool OverviewTab::loadFileToEditor(const QString& path, QPlainTextEdit* editor, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (err) *err = f.errorString();
        return false;
    }
    QTextStream in(&f);
    const bool blocked = editor->blockSignals(true);
    editor->setPlainText(in.readAll());
    editor->blockSignals(blocked);
    if (editor == jsEdit_ && jsUnsavedLabel_) jsUnsavedLabel_->setVisible(false);
    return true;
}

bool OverviewTab::saveEditorToFile(QPlainTextEdit* editor, const QString& path, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (err) *err = f.errorString();
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
    if (path == defaultJsPath()) {
        QString err;
        if (!ensureDefaultsFile(path, ":/defaults/overviewConfig.js", &err)) {
            SPDLOG_WARN("ensureDefaultsFile(overviewConfig.js) failed: {}", err.toStdString());
        }
    }
    QString err;
    if (!loadFileToEditor(path, jsEdit_, &err)) {
        SPDLOG_WARN("Failed to load overviewConfig.js from {}: {}", path.toStdString(), err.toStdString());
        QMessageBox::warning(this, tr("Reset overviewConfig.js"), tr("Failed to load: %1").arg(err));
        return;
    }
    jsPathLabel_->setText(path);
    if (jsUnsavedLabel_) jsUnsavedLabel_->setVisible(false);
}

void OverviewTab::onSaveJs()
{
    const QString path = currentJsPath();
    QString err;
    if (!saveEditorToFile(jsEdit_, path, &err)) {
        SPDLOG_ERROR("Failed to save overviewConfig.js to {}: {}", path.toStdString(), err.toStdString());
        QMessageBox::warning(this, tr("Save overviewConfig.js"), tr("Failed to save: %1").arg(err));
        return;
    }
    QMessageBox::information(this, tr("Save overviewConfig.js"), tr("Saved."));
    if (jsUnsavedLabel_) jsUnsavedLabel_->setVisible(false);
}

void OverviewTab::onApplyJs()
{
    const QString path = currentJsPath();
    QString err;
    // Always save first to ensure the latest content is applied
    {
        QString saveErr;
        if (!saveEditorToFile(jsEdit_, path, &saveErr)) {
            QMessageBox::warning(this, tr("Apply Camera Script"), tr("Failed to save script: %1").arg(saveErr));
            return;
        }
    }

    std::string backendErr;
    if (!backend_.applyCameraScriptFromFile(path.toStdString(), &backendErr)) {
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
    if (selected.isEmpty()) return;
    {
        QSettings s;
        s.setValue("Config/ExternalOverviewScriptPath", selected);
    }
    SPDLOG_INFO("External Overview script set to {}", selected.toStdString());
    QString err;
    if (!loadFileToEditor(selected, jsEdit_, &err)) {
        SPDLOG_WARN("Failed to load external overviewConfig.js from {}: {}", selected.toStdString(), err.toStdString());
        QMessageBox::warning(this, tr("Reset overviewConfig.js"), tr("Failed to load: %1").arg(err));
        return;
    }
    jsPathLabel_->setText(selected);
    if (jsUnsavedLabel_) jsUnsavedLabel_->setVisible(false);
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
    if (ret == QMessageBox::Yes) {
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

} // namespace frontend
