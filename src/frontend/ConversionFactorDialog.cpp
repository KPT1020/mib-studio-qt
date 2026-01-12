#include "frontend/ConversionFactorDialog.h"

#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QTextStream>

#include <spdlog/spdlog.h>
#ifdef _WIN32
#define NOMINMAX  // Prevent Windows.h from defining min/max macros
#include <windows.h>
#include <shlobj.h>
#endif

#include "backend/AppBackend.h"
#include "backend/services/ProcessingService.h"

namespace
{
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

    static QString getConfigPath() {
        QSettings s;
        const QString ext = s.value("Config/ExternalAppConfigPath").toString().trimmed();
        if (!ext.isEmpty()) return ext;
        return QDir(getUserConfigDir()).absoluteFilePath("config.json");
    }
}

ConversionFactorDialog::ConversionFactorDialog(backend::AppBackend& backend, QWidget* parent)
    : QDialog(parent), backend_(backend) {
    setWindowTitle(tr("Pixel to Micron Conversion"));
    setModal(true);

    auto* layout = new QFormLayout(this);

    conversionFactorSpin_ = new QDoubleSpinBox(this);
    conversionFactorSpin_->setMinimum(0.0001);
    conversionFactorSpin_->setMaximum(1000.0);
    conversionFactorSpin_->setDecimals(4);
    conversionFactorSpin_->setSuffix(tr(" μm/pixel"));
    conversionFactorSpin_->setToolTip(tr("Conversion factor: 1 pixel = X micron"));

    layout->addRow(tr("Pixel to Micron Factor"), conversionFactorSpin_);

    // Load current value from backend
    conversionFactorSpin_->setValue(backend_.processing().getPixelToMicronFactor());

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    layout->addRow(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, [this]() {
        applySettings();
        accept();
    });
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons_->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this](bool) { onApply(); });
}

void ConversionFactorDialog::onApply() {
    applySettings();
}

void ConversionFactorDialog::applySettings() {
    auto& proc = backend_.processing();
    const double factor = conversionFactorSpin_->value();

    proc.setPixelToMicronFactor(factor);
    SPDLOG_INFO("Pixel to micron conversion factor applied: {}", factor);
    
    // Save to config file
    saveConversionFactorToConfig(factor);
}

void ConversionFactorDialog::saveConversionFactorToConfig(double factor) {
    const QString configPath = getConfigPath();
    QFile file(configPath);
    
    if (!file.exists()) {
        SPDLOG_DEBUG("ConversionFactorDialog: config.json does not exist, skipping conversion factor save");
        return;
    }
    
    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        SPDLOG_WARN("ConversionFactorDialog: failed to open config.json for conversion factor save: {}", file.errorString().toStdString());
        return;
    }
    
    QByteArray data = file.readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        SPDLOG_WARN("ConversionFactorDialog: failed to parse config.json for conversion factor save: {}", parseError.errorString().toStdString());
        file.close();
        return;
    }
    
    if (!doc.isObject()) {
        SPDLOG_WARN("ConversionFactorDialog: config.json root is not an object, skipping conversion factor save");
        file.close();
        return;
    }
    
    QJsonObject root = doc.object();
    
    // Save conversion factor to config
    root.insert("pixel_to_micron_factor", factor);
    doc.setObject(root);
    
    // Write back to file
    file.resize(0);
    file.seek(0);
    QTextStream out(&file);
    out << doc.toJson(QJsonDocument::Indented);
    file.close();
    
    SPDLOG_DEBUG("ConversionFactorDialog: saved pixel_to_micron_factor={} to config.json", factor);
}
