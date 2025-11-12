#include "frontend/ConfigTabs.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QStandardPaths>

#include <spdlog/spdlog.h>

#include "backend/AppBackend.h"

namespace frontend {

ConfigTabs::ConfigTabs(backend::AppBackend& backend, QWidget* parent)
    : QWidget(parent), backend_(backend) {
    auto* layout = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);

    // App JSON config tab
    {
        auto* page = new QWidget(this);
        auto* v = new QVBoxLayout(page);
        jsonEdit_ = new QPlainTextEdit(page);
        jsonEdit_->setWordWrapMode(QTextOption::NoWrap);
        auto* row = new QHBoxLayout();
        jsonReloadBtn_ = new QPushButton(tr("Reload"), page);
        jsonSaveBtn_ = new QPushButton(tr("Save"), page);
        jsonPathLabel_ = new QLabel(page);
        row->addWidget(jsonReloadBtn_);
        row->addWidget(jsonSaveBtn_);
        row->addStretch(1);
        row->addWidget(jsonPathLabel_);
        v->addLayout(row);
        v->addWidget(jsonEdit_, 1);
        page->setLayout(v);
        tabs_->addTab(page, tr("App config (config.json)"));
        connect(jsonReloadBtn_, &QPushButton::clicked, this, &ConfigTabs::onReloadJson);
        connect(jsonSaveBtn_, &QPushButton::clicked, this, &ConfigTabs::onSaveJson);
    }

    // Camera JS script tab
    {
        auto* page = new QWidget(this);
        auto* v = new QVBoxLayout(page);
        jsEdit_ = new QPlainTextEdit(page);
        jsEdit_->setWordWrapMode(QTextOption::NoWrap);
        auto* row = new QHBoxLayout();
        jsReloadBtn_ = new QPushButton(tr("Reload"), page);
        jsSaveBtn_ = new QPushButton(tr("Save"), page);
        jsApplyBtn_ = new QPushButton(tr("Apply to Camera"), page);
        jsPathLabel_ = new QLabel(page);
        row->addWidget(jsReloadBtn_);
        row->addWidget(jsSaveBtn_);
        row->addWidget(jsApplyBtn_);
        row->addStretch(1);
        row->addWidget(jsPathLabel_);
        v->addLayout(row);
        v->addWidget(jsEdit_, 1);
        page->setLayout(v);
        tabs_->addTab(page, tr("Camera script (egrabberConfig.js)"));
        connect(jsReloadBtn_, &QPushButton::clicked, this, &ConfigTabs::onReloadJs);
        connect(jsSaveBtn_, &QPushButton::clicked, this, &ConfigTabs::onSaveJs);
        connect(jsApplyBtn_, &QPushButton::clicked, this, &ConfigTabs::onApplyJs);
    }

    layout->addWidget(tabs_, 1);

    onReloadJson();
    onReloadJs();
}

QString ConfigTabs::appDirIncludePath(const QString& fileName) const {
    const QString appDir = QCoreApplication::applicationDirPath();
    // Write into a sibling include directory next to the executable dir (build tree)
    return QDir(appDir).absoluteFilePath("../include/" + fileName);
}

bool ConfigTabs::loadFileToEditor(const QString& path, QPlainTextEdit* editor, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (err) *err = f.errorString();
        return false;
    }
    QTextStream in(&f);
    editor->setPlainText(in.readAll());
    return true;
}

bool ConfigTabs::saveEditorToFile(QPlainTextEdit* editor, const QString& path, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (err) *err = f.errorString();
        return false;
    }
    QTextStream out(&f);
    out << editor->toPlainText();
    return true;
}

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

void ConfigTabs::onReloadJson() {
    const QString path = appDirIncludePath("config.json");
    {
        QString err;
        if (!ensureDefaultsFile(path, ":/defaults/config.json", &err)) {
            SPDLOG_WARN("ensureDefaultsFile(config.json) failed: {}", err.toStdString());
        }
    }
    QString err;
    if (!loadFileToEditor(path, jsonEdit_, &err)) {
        SPDLOG_WARN("Failed to load config.json from {}: {}", path.toStdString(), err.toStdString());
        QMessageBox::warning(this, tr("Load config.json"), tr("Failed to load: %1").arg(err));
        return;
    }
    jsonPathLabel_->setText(path);
}

void ConfigTabs::onSaveJson() {
    const QString path = appDirIncludePath("config.json");
    QString err;
    if (!saveEditorToFile(jsonEdit_, path, &err)) {
        SPDLOG_ERROR("Failed to save config.json to {}: {}", path.toStdString(), err.toStdString());
        QMessageBox::warning(this, tr("Save config.json"), tr("Failed to save: %1").arg(err));
        return;
    }
    QMessageBox::information(this, tr("Save config.json"), tr("Saved."));
}

void ConfigTabs::onReloadJs() {
    const QString path = appDirIncludePath("egrabberConfig.js");
    {
        QString err;
        if (!ensureDefaultsFile(path, ":/defaults/egrabberConfig.js", &err)) {
            SPDLOG_WARN("ensureDefaultsFile(egrabberConfig.js) failed: {}", err.toStdString());
        }
    }
    QString err;
    if (!loadFileToEditor(path, jsEdit_, &err)) {
        SPDLOG_WARN("Failed to load egrabberConfig.js from {}: {}", path.toStdString(), err.toStdString());
        QMessageBox::warning(this, tr("Load egrabberConfig.js"), tr("Failed to load: %1").arg(err));
        return;
    }
    jsPathLabel_->setText(path);
}

void ConfigTabs::onSaveJs() {
    const QString path = appDirIncludePath("egrabberConfig.js");
    QString err;
    if (!saveEditorToFile(jsEdit_, path, &err)) {
        SPDLOG_ERROR("Failed to save egrabberConfig.js to {}: {}", path.toStdString(), err.toStdString());
        QMessageBox::warning(this, tr("Save egrabberConfig.js"), tr("Failed to save: %1").arg(err));
        return;
    }
    QMessageBox::information(this, tr("Save egrabberConfig.js"), tr("Saved."));
}

void ConfigTabs::onApplyJs() {
    const QString path = appDirIncludePath("egrabberConfig.js");
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

} // namespace frontend



