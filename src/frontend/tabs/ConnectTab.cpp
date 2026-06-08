#include "frontend/tabs/ConnectTab.h"
#include "ui_ConnectTab.h"

#include <QMessageBox>
#include <QVariant>

#include <spdlog/spdlog.h>

#include "backend/AppBackend.h"
#include "backend/services/CameraControlService.h"
#include "backend/services/CaptureService.h"
#include "frontend/dialogs/MockConfigDialog.h"
#include "frontend/system/DeviceInitManager.h"
#include "backend/camera/mock/MockCamera.h"

#include <filesystem>
#include <algorithm>
#include <chrono>

namespace {

struct DeviceIdx {
    int ifIndex = -1;
    int devIndex = -1;
};

QVariant toVariant(const DeviceIdx& d) {
    return QVariant::fromValue<QString>(QString("%1:%2").arg(d.ifIndex).arg(d.devIndex));
}

bool fromVariant(const QVariant& v, DeviceIdx& out) {
    const auto s = v.toString();
    const auto parts = s.split(':');
    if (parts.size() != 2) return false;
    bool ok1 = false, ok2 = false;
    const int a = parts[0].toInt(&ok1);
    const int b = parts[1].toInt(&ok2);
    if (!ok1 || !ok2) return false;
    out.ifIndex = a;
    out.devIndex = b;
    return true;
}

} // namespace

namespace frontend {

ConnectTab::ConnectTab(backend::AppBackend& backend, QWidget* parent)
    : QWidget(parent), ui(new Ui::ConnectTab), backend_(backend) {
    ui->setupUi(this);

    connect(ui->refreshBtn, &QPushButton::clicked, this, &ConnectTab::onRefresh);
    connect(ui->connectBtn, &QPushButton::clicked, this, &ConnectTab::onConnect);
    connect(ui->mockBtn, &QPushButton::clicked, this, &ConnectTab::onConfigureMock);

    onRefresh();
}

ConnectTab::~ConnectTab() {
    delete ui;
}

void ConnectTab::onRefresh() {
    populateDevices();
}

void ConnectTab::tryAutoConnect()
{
    if (initManager_) {
        initManager_->runCameraStep();
        return;
    }

    // Fallback when no DeviceInitManager: run discovery on UI thread (may block)
    if (backend_.capture().isRunning())
    {
        SPDLOG_INFO("ConnectTab: auto-connect skipped (capture running)");
        return;
    }
    if (backend_.isCameraConfigured())
    {
        SPDLOG_INFO("ConnectTab: auto-connect skipped (camera already configured)");
        return;
    }

    auto &cc = backend_.cameraControl();
    const auto cameras = cc.discoverCameras();

    SPDLOG_INFO("ConnectTab: auto-connect discovery found {} camera(s)", cameras.size());

    if (cameras.empty())
    {
        ui->statusLabel->setText(tr("No cameras found."));
        emit noCamerasFound();
        return;
    }

    if (cameras.size() == 1)
    {
        const auto &cam = cameras[0];
        backend_.setHardwareCameraSelection(cam.interfaceIndex, cam.deviceIndex, cam.label);
        applyCameraSelection(cam.interfaceIndex, cam.deviceIndex, QString::fromStdString(cam.label));
        return;
    }

    ui->statusLabel->setText(tr("Multiple cameras found; select one and click Connect."));
}

void ConnectTab::applyCameraSelection(int interfaceIndex, int deviceIndex, const QString& label)
{
    ui->statusLabel->setText(tr("Connected to %1 (not capturing)").arg(label));
    ui->tabWidget->setCurrentIndex(0);
    for (int i = 0; i < ui->cameraList->count(); ++i)
    {
        auto *item = ui->cameraList->item(i);
        if (item && item->text() == label)
        {
            ui->cameraList->setCurrentRow(i);
            break;
        }
    }
    emit connected();
}

void ConnectTab::reportNoCameras()
{
    ui->statusLabel->setText(tr("No cameras found."));
    emit noCamerasFound();
}

void ConnectTab::reportMultipleCameras()
{
    ui->statusLabel->setText(tr("Multiple cameras found; select one and click Connect."));
}

void ConnectTab::populateDevices() {
    ui->framegrabberList->clear();
    ui->cameraList->clear();

    auto& cc = backend_.cameraControl();
    
    // Populate framegrabbers tab
    const auto framegrabbers = cc.discoverFramegrabbers();
    for (const auto& fg : framegrabbers) {
        auto* item = new QListWidgetItem(QString::fromStdString(fg.label));
        item->setData(Qt::UserRole, toVariant(DeviceIdx{fg.interfaceIndex, fg.deviceIndex}));
        ui->framegrabberList->addItem(item);
    }
    
    if (ui->framegrabberList->count() > 0) {
        ui->framegrabberList->setCurrentRow(0);
    }
    
    // Populate cameras tab
    const auto cameras = cc.discoverCameras();
    for (const auto& cam : cameras) {
        auto* item = new QListWidgetItem(QString::fromStdString(cam.label));
        item->setData(Qt::UserRole, toVariant(DeviceIdx{cam.interfaceIndex, cam.deviceIndex}));
        ui->cameraList->addItem(item);
    }
    
    if (ui->cameraList->count() > 0) {
        ui->cameraList->setCurrentRow(0);
    }

    ui->statusLabel->setText(QString("Found %1 framegrabber(s), %2 camera(s)")
                         .arg(ui->framegrabberList->count())
                         .arg(ui->cameraList->count()));
    SPDLOG_INFO("ConnectTab: refreshed, {} framegrabber(s), {} camera(s) listed", 
                ui->framegrabberList->count(), ui->cameraList->count());
}

void ConnectTab::onConnect() {
    // Guard: Cannot change camera connection while camera is running
    if (backend_.capture().isRunning()) {
        QMessageBox::warning(this, tr("Connect Device"),
                             tr("Cannot change camera connection while camera is running. Please stop the camera first."));
        return;
    }

    // Get the current tab and its list widget
    QListWidget* currentList = nullptr;
    QString deviceType;
    
    int currentTab = ui->tabWidget->currentIndex();
    if (currentTab == 0) {
        currentList = ui->cameraList;
        deviceType = tr("camera");
    } else if (currentTab == 1) {
        currentList = ui->framegrabberList;
        deviceType = tr("framegrabber");
    } else {
        QMessageBox::information(this, tr("Connect Device"), tr("Please select a device from the list."));
        return;
    }
    
    const auto* item = currentList->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Connect Device"), 
                                 tr("Please select a %1 from the list.").arg(deviceType));
        return;
    }

    DeviceIdx idx{};
    if (!fromVariant(item->data(Qt::UserRole), idx)) {
        QMessageBox::warning(this, tr("Connect Device"), tr("Internal error: invalid selection."));
        return;
    }

    const QString label = item->text();
    SPDLOG_INFO("ConnectTab: selecting hardware device {} ({}:{})",
                label.toStdString(), idx.ifIndex, idx.devIndex);

    backend_.setHardwareCameraSelection(idx.ifIndex, idx.devIndex, label.toStdString());
    ui->statusLabel->setText(tr("Connected to %1 (not capturing)").arg(label));
    emit connected();
}

void ConnectTab::onConfigureMock() {
    // Guard: Cannot change camera connection while camera is running
    if (backend_.capture().isRunning()) {
        QMessageBox::warning(this, tr("Configure Mock Camera"),
                             tr("Cannot change camera connection while camera is running. Please stop the camera first."));
        return;
    }

    frontend::MockConfigDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    camera::mock::MockCameraOptions options{};
#ifdef _WIN32
    options.folder = std::filesystem::path(dlg.folderPath().toStdWString());
#else
    options.folder = std::filesystem::path(dlg.folderPath().toStdString());
#endif
    // Clamp FPS to >= 1 without relying on std::max (avoid Windows macros)
    double fps = dlg.framesPerSecond();
    if (fps < 1.0) fps = 1.0;
    const auto micros = static_cast<long long>(1'000'000.0 / fps);
    options.frameInterval = std::chrono::microseconds(micros);
    options.loopFiles = true;

    backend_.configureMockCamera(options);
    ui->statusLabel->setText(tr("Mock camera configured (not capturing)"));
    SPDLOG_INFO("ConnectTab: mock camera configured ({}, ~{} fps)",
                options.folder.string(),
                options.frameInterval.count() > 0 ? 1'000'000.0 / options.frameInterval.count() : 0.0);
    emit connected();
}

} // namespace frontend



