#include "frontend/ConnectTab.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QMessageBox>
#include <QLabel>
#include <QVariant>
#include <QApplication>
#include <QIcon>
#include <QTabWidget>
#include <QWidget>

#include <spdlog/spdlog.h>

#include "backend/AppBackend.h"
#include "backend/services/CameraControlService.h"
#include "frontend/MockConfigDialog.h"
#include "camera/mock/MockCamera.h"

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
    : QWidget(parent), backend_(backend) {
    auto* root = new QVBoxLayout(this);

    // Create tab widget with two tabs
    tabWidget_ = new QTabWidget(this);
    
    // Framegrabbers tab
    auto* framegrabberWidget = new QWidget(this);
    auto* framegrabberLayout = new QVBoxLayout(framegrabberWidget);
    framegrabberList_ = new QListWidget(framegrabberWidget);
    framegrabberList_->setSelectionMode(QAbstractItemView::SingleSelection);
    framegrabberLayout->addWidget(framegrabberList_);
    framegrabberLayout->setContentsMargins(0, 0, 0, 0);
    tabWidget_->addTab(framegrabberWidget, tr("Framegrabbers"));
    
    // Cameras tab
    auto* cameraWidget = new QWidget(this);
    auto* cameraLayout = new QVBoxLayout(cameraWidget);
    cameraList_ = new QListWidget(cameraWidget);
    cameraList_->setSelectionMode(QAbstractItemView::SingleSelection);
    cameraLayout->addWidget(cameraList_);
    cameraLayout->setContentsMargins(0, 0, 0, 0);
    tabWidget_->addTab(cameraWidget, tr("Cameras"));

    auto* buttonsRow = new QHBoxLayout();
    refreshBtn_ = new QPushButton(tr("Refresh"), this);
    connectBtn_ = new QPushButton(tr("Connect"), this);
    mockBtn_ = new QPushButton(tr("Configure Mock…"), this);
    buttonsRow->addWidget(refreshBtn_);
    buttonsRow->addWidget(connectBtn_);
    buttonsRow->addStretch(1);
    buttonsRow->addWidget(mockBtn_);

    statusLabel_ = new QLabel(tr("Select a device and click Connect."), this);

    root->addWidget(new QLabel(tr("Available devices:"), this));
    root->addWidget(tabWidget_, 1);
    root->addLayout(buttonsRow);
    root->addWidget(statusLabel_);

    connect(refreshBtn_, &QPushButton::clicked, this, &ConnectTab::onRefresh);
    connect(connectBtn_, &QPushButton::clicked, this, &ConnectTab::onConnect);
    connect(mockBtn_, &QPushButton::clicked, this, &ConnectTab::onConfigureMock);

    onRefresh();
}

void ConnectTab::onRefresh() {
    populateDevices();
}

void ConnectTab::populateDevices() {
    framegrabberList_->clear();
    cameraList_->clear();

    auto& cc = backend_.cameraControl();
    
    // Populate framegrabbers tab
    const auto framegrabbers = cc.discoverFramegrabbers();
    for (const auto& fg : framegrabbers) {
        auto* item = new QListWidgetItem(QString::fromStdString(fg.label));
        item->setData(Qt::UserRole, toVariant(DeviceIdx{fg.interfaceIndex, fg.deviceIndex}));
        framegrabberList_->addItem(item);
    }
    
    if (framegrabberList_->count() > 0) {
        framegrabberList_->setCurrentRow(0);
    }
    
    // Populate cameras tab
    const auto cameras = cc.discoverCameras();
    for (const auto& cam : cameras) {
        auto* item = new QListWidgetItem(QString::fromStdString(cam.label));
        item->setData(Qt::UserRole, toVariant(DeviceIdx{cam.interfaceIndex, cam.deviceIndex}));
        cameraList_->addItem(item);
    }
    
    if (cameraList_->count() > 0) {
        cameraList_->setCurrentRow(0);
    }

    statusLabel_->setText(QString("Found %1 framegrabber(s), %2 camera(s)")
                         .arg(framegrabberList_->count())
                         .arg(cameraList_->count()));
    SPDLOG_INFO("ConnectTab: refreshed, {} framegrabber(s), {} camera(s) listed", 
                framegrabberList_->count(), cameraList_->count());
}

void ConnectTab::onConnect() {
    // Get the current tab and its list widget
    QListWidget* currentList = nullptr;
    QString deviceType;
    
    int currentTab = tabWidget_->currentIndex();
    if (currentTab == 0) {
        currentList = framegrabberList_;
        deviceType = tr("framegrabber");
    } else if (currentTab == 1) {
        currentList = cameraList_;
        deviceType = tr("camera");
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
    statusLabel_->setText(tr("Connected to %1 (not capturing)").arg(label));
    emit connected();
}

void ConnectTab::onConfigureMock() {
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
    statusLabel_->setText(tr("Mock camera configured (not capturing)"));
    SPDLOG_INFO("ConnectTab: mock camera configured ({}, ~{} fps)",
                options.folder.string(),
                options.frameInterval.count() > 0 ? 1'000'000.0 / options.frameInterval.count() : 0.0);
    emit connected();
}

} // namespace frontend



