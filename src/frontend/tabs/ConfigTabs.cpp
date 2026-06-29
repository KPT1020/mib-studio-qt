#include "frontend/tabs/ConfigTabs.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
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
#include <QStackedWidget>
#include <QTableView>
#include <QToolButton>
#include <QTimer>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QHeaderView>
#include <QJsonObject>
#include <QJsonArray>
#include <QComboBox>
#include <QCheckBox>
#include <QInputDialog>
#include <QRegularExpression>
#include <QScrollArea>
#include <QGridLayout>
#include <QGroupBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>

#include <spdlog/spdlog.h>
#ifdef _WIN32
#define NOMINMAX  // Prevent Windows.h from defining min/max macros
#include <windows.h>
#include <shlobj.h>
#endif

#include "backend/app/AppBackend.h"
#include "frontend/system/ProfileManager.h"
#include "frontend/models/JsonTableModel.h"
#include "frontend/utils/JsonFlatten.h"

namespace frontend {

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

} // namespace

ConfigTabs::ConfigTabs(backend::AppBackend& backend, QWidget* parent)
    : QWidget(parent), backend_(backend) {
    auto* layout = new QVBoxLayout(this);
    // Important: let parent QSplitter shrink this widget.
    // Default layout constraints can impose a large minimum size, making splitter dragging feel “stuck”.
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    tabs_ = new QTabWidget(this);

    // App JSON config tab
    {
        auto* page = new QWidget(this);
        auto* v = new QVBoxLayout(page);
        jsonEdit_ = new QPlainTextEdit(page);
        jsonEdit_->setWordWrapMode(QTextOption::NoWrap);
        auto* row = new QHBoxLayout();
        jsonReloadBtn_ = new QPushButton(tr("Reset"), page);
        jsonSaveBtn_ = new QPushButton(tr("Save"), page);
        jsonBrowseBtn_ = new QPushButton(tr("Browse..."), page);
        jsonClearBtn_ = new QPushButton(tr("Clear"), page);
		jsonTableToggle_ = new QToolButton(page);
		jsonTableToggle_->setText(tr("json/table"));
        jsonTableToggle_->setToolTip(tr("Toggle table view"));
        jsonTableToggle_->setCheckable(true);
        jsonPathLabel_ = new QLabel(page);
        jsonPathLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        jsonPathLabel_->setTextFormat(Qt::PlainText);
        jsonPathLabel_->setWordWrap(false);
        jsonUnsavedLabel_ = new QLabel(page);
        jsonUnsavedLabel_->setText(tr("Unsaved changes – click Save to apply."));
        jsonUnsavedLabel_->setVisible(false);
        jsonUnsavedLabel_->setStyleSheet("color: #d17a00;");
        jsonUnsavedLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        jsonConflictLabel_ = new QLabel(page);
        jsonConflictLabel_->setText(tr("Config changed elsewhere; reload before saving if you want the latest file."));
        jsonConflictLabel_->setVisible(false);
        jsonConflictLabel_->setStyleSheet("color: #b00020;");
        jsonConflictLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        profileSelect_ = new QComboBox(page);
        saveProfileBtn_ = new QPushButton(tr("Save Profile"), page);
        deleteProfileBtn_ = new QPushButton(tr("Delete"), page);
        renameProfileBtn_ = new QPushButton(tr("Rename"), page);
        checkUpdatesBtn_ = new QPushButton(tr("Check Updates"), page);
        updateSelectedBtn_ = new QPushButton(tr("Update Selected"), page);
        showDiffBtn_ = new QPushButton(tr("Show Diff"), page);
        duplicateAsLocalBtn_ = new QPushButton(tr("Duplicate as Local"), page);
        profileStatusLabel_ = new QLabel(page);
        profileStatusLabel_->setText(tr("No profile selected"));
        profileStatusLabel_->setTextFormat(Qt::PlainText);
        profileStatusLabel_->setWordWrap(false);
        profileStatusLabel_->setMinimumWidth(0);
        profileStatusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        row->addWidget(jsonReloadBtn_);
        row->addWidget(jsonSaveBtn_);
        row->addWidget(jsonBrowseBtn_);
        row->addWidget(jsonClearBtn_);
        row->addWidget(jsonTableToggle_);
        row->addStretch(1);
        row->addWidget(jsonPathLabel_);
        row->addSpacing(8);
        row->addWidget(jsonUnsavedLabel_);
        row->addSpacing(8);
        row->addWidget(jsonConflictLabel_);
        auto* rowWidget = new QWidget(page);
        rowWidget->setLayout(row);
        auto* rowScroll = new QScrollArea(page);
        rowScroll->setWidget(rowWidget);
        rowScroll->setWidgetResizable(true);
        rowScroll->setFrameShape(QFrame::NoFrame);
        rowScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        rowScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        rowScroll->setFixedHeight(rowWidget->sizeHint().height() + 4);
        v->addWidget(rowScroll);

        auto* profileRow = new QHBoxLayout();
        profileRow->addWidget(new QLabel(tr("Profile:"), page));
        profileRow->addWidget(profileSelect_);
        profileRow->addWidget(saveProfileBtn_);
        profileRow->addWidget(renameProfileBtn_);
        profileRow->addWidget(deleteProfileBtn_);
        profileRow->addWidget(checkUpdatesBtn_);
        profileRow->addWidget(updateSelectedBtn_);
        profileRow->addWidget(showDiffBtn_);
        profileRow->addWidget(duplicateAsLocalBtn_);
        profileRow->addStretch(1);
        profileRow->addWidget(profileStatusLabel_);
        auto* profileRowWidget = new QWidget(page);
        profileRowWidget->setLayout(profileRow);
        auto* profileRowScroll = new QScrollArea(page);
        profileRowScroll->setWidget(profileRowWidget);
        profileRowScroll->setWidgetResizable(true);
        profileRowScroll->setFrameShape(QFrame::NoFrame);
        profileRowScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        profileRowScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        profileRowScroll->setFixedHeight(profileRowWidget->sizeHint().height() + 4);
        v->addWidget(profileRowScroll);

        // Legacy single table (for backward compatibility)
        jsonModel_ = new JsonTableModel(this);
        jsonTable_ = new QTableView();
        jsonTable_->setModel(jsonModel_);
        jsonTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        jsonTable_->setSelectionMode(QAbstractItemView::SingleSelection);
        jsonTable_->setAlternatingRowColors(true);
		jsonTable_->setSortingEnabled(false);
		jsonTable_->setWordWrap(true);
		jsonTable_->setTextElideMode(Qt::ElideNone);
		jsonTable_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
		jsonTable_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		jsonTable_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

        jsonStack_ = new QStackedWidget(page);
        
        // New grouped tables layout
        jsonScrollArea_ = new QScrollArea();
        jsonScrollArea_->setWidgetResizable(true);
        jsonScrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        jsonScrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        
        jsonGridContainer_ = new QWidget();
        jsonGridLayout_ = new QGridLayout(jsonGridContainer_);
        jsonGridLayout_->setColumnStretch(0, 1);
        jsonGridLayout_->setColumnStretch(1, 1);
        jsonGridLayout_->setColumnStretch(2, 1);
        jsonGridLayout_->setSpacing(10);
        jsonGridLayout_->setContentsMargins(5, 5, 5, 5);
        
        jsonScrollArea_->setWidget(jsonGridContainer_);

        jsonStack_->addWidget(jsonEdit_);
        jsonStack_->addWidget(jsonScrollArea_);  // Use scroll area instead of single table
        // Legacy table is not added to stack - it's kept for backward compatibility but hidden
        if (jsonTable_) {
            jsonTable_->setParent(nullptr);
            jsonTable_->hide();
        }
        v->addWidget(jsonStack_, 1);

        // Persist toggle choice
        {
            QSettings s;
            const bool showTable = s.value("Preview/ShowTable", true).toBool();
            jsonTableToggle_->setChecked(showTable);
            jsonStack_->setCurrentIndex(showTable ? 1 : 0);
        }

        connect(jsonTableToggle_, &QToolButton::toggled, this, &ConfigTabs::onJsonTableToggled);
		connect(jsonModel_, &QAbstractItemModel::dataChanged, this,
		        [this](const QModelIndex&, const QModelIndex&, const QVector<int>&) { rebuildJsonFromTable(); });

        // Debounced updates when editing JSON while table is visible
        jsonDebounceTimer_ = new QTimer(this);
        jsonDebounceTimer_->setSingleShot(true);
        jsonDebounceTimer_->setInterval(150);
        connect(jsonDebounceTimer_, &QTimer::timeout, this, &ConfigTabs::onJsonTextChangedDebounced);
        connect(jsonEdit_, &QPlainTextEdit::textChanged, this, [this]() {
            if (jsonStack_ && jsonStack_->currentIndex() == 1) {
                jsonDebounceTimer_->start();
            }
            if (jsonUnsavedLabel_) jsonUnsavedLabel_->setVisible(true);
        });

        page->setLayout(v);
        tabs_->addTab(page, tr("App config (config.json)"));
        connect(jsonReloadBtn_, &QPushButton::clicked, this, &ConfigTabs::onReloadJson);
        connect(jsonSaveBtn_, &QPushButton::clicked, this, &ConfigTabs::onSaveJson);
        connect(jsonBrowseBtn_, &QPushButton::clicked, this, &ConfigTabs::onBrowseJson);
        connect(jsonClearBtn_, &QPushButton::clicked, this, &ConfigTabs::onClearJson);
    }

    // Camera JS script tab
    {
        auto* page = new QWidget(this);
        auto* v = new QVBoxLayout(page);
        jsEdit_ = new QPlainTextEdit(page);
        jsEdit_->setWordWrapMode(QTextOption::NoWrap);
        auto* row = new QHBoxLayout();
        jsReloadBtn_ = new QPushButton(tr("Reset"), page);
        jsSaveBtn_ = new QPushButton(tr("Save"), page);
        jsApplyBtn_ = new QPushButton(tr("Apply to Camera"), page);
        jsResetBtn_ = new QPushButton(tr("Reset Camera"), page);
        jsBrowseBtn_ = new QPushButton(tr("Browse..."), page);
        jsClearBtn_ = new QPushButton(tr("Clear"), page);
        jsPathLabel_ = new QLabel(page);
        jsPathLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        jsUnsavedLabel_ = new QLabel(page);
        jsUnsavedLabel_->setText(tr("Unsaved changes – click Save to apply."));
        jsUnsavedLabel_->setVisible(false);
        jsUnsavedLabel_->setStyleSheet("color: #d17a00;");
        profilesIncludeJsCheck_ = new QCheckBox(tr("Profiles include Camera script"), page);
        {
            QSettings s;
            profilesIncludeJsCheck_->setChecked(s.value("Profiles/IncludeJs", true).toBool());
        }
        row->addWidget(jsReloadBtn_);
        row->addWidget(jsSaveBtn_);
        row->addWidget(jsApplyBtn_);
        row->addWidget(jsResetBtn_);
        row->addWidget(jsBrowseBtn_);
        row->addWidget(jsClearBtn_);
        row->addStretch(1);
        row->addWidget(jsPathLabel_);
        row->addSpacing(8);
        row->addWidget(jsUnsavedLabel_);
        row->addSpacing(8);
        row->addWidget(profilesIncludeJsCheck_);
        auto* jsRowWidget = new QWidget(page);
        jsRowWidget->setLayout(row);
        auto* jsRowScroll = new QScrollArea(page);
        jsRowScroll->setWidget(jsRowWidget);
        jsRowScroll->setWidgetResizable(true);
        jsRowScroll->setFrameShape(QFrame::NoFrame);
        jsRowScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        jsRowScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        jsRowScroll->setFixedHeight(jsRowWidget->sizeHint().height() + 4);
        v->addWidget(jsRowScroll);
        v->addWidget(jsEdit_, 1);
        page->setLayout(v);
        tabs_->addTab(page, tr("Camera script (egrabberConfig.js)"));
        connect(jsReloadBtn_, &QPushButton::clicked, this, &ConfigTabs::onReloadJs);
        connect(jsSaveBtn_, &QPushButton::clicked, this, &ConfigTabs::onSaveJs);
        connect(jsApplyBtn_, &QPushButton::clicked, this, &ConfigTabs::onApplyJs);
        connect(jsResetBtn_, &QPushButton::clicked, this, &ConfigTabs::onResetCamera);
        connect(jsBrowseBtn_, &QPushButton::clicked, this, &ConfigTabs::onBrowseJs);
        connect(jsClearBtn_, &QPushButton::clicked, this, &ConfigTabs::onClearJs);
        connect(jsEdit_, &QPlainTextEdit::textChanged, this, [this]() {
            if (jsUnsavedLabel_) jsUnsavedLabel_->setVisible(true);
        });
        connect(profilesIncludeJsCheck_, &QCheckBox::toggled, this, &ConfigTabs::onIncludeJsToggled);
    }

    layout->addWidget(tabs_, 1);

    onReloadJson();
    onReloadJs();

    // Profiles: populate and wire
    refreshProfilesList();
    connect(profileSelect_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ConfigTabs::onProfileSelectionChanged);
    connect(saveProfileBtn_, &QPushButton::clicked, this, &ConfigTabs::onSaveProfile);
    connect(deleteProfileBtn_, &QPushButton::clicked, this, &ConfigTabs::onDeleteProfile);
    connect(renameProfileBtn_, &QPushButton::clicked, this, &ConfigTabs::onRenameProfile);
    connect(checkUpdatesBtn_, &QPushButton::clicked, this, &ConfigTabs::onCheckProfileUpdates);
    connect(updateSelectedBtn_, &QPushButton::clicked, this, &ConfigTabs::onUpdateSelectedProfile);
    connect(showDiffBtn_, &QPushButton::clicked, this, &ConfigTabs::onShowProfileDiff);
    connect(duplicateAsLocalBtn_, &QPushButton::clicked, this, &ConfigTabs::onDuplicateProfileAsLocal);
    refreshProfileStatusLabel();
}

QString ConfigTabs::appDirIncludePath(const QString& fileName) const {
    // Use centralized helper to get user-writable config directory
    return QDir(getUserConfigDir()).absoluteFilePath(fileName);
}

QString ConfigTabs::currentJsonPath() const {
    QSettings s;
    const QString ext = s.value("Config/ExternalAppConfigPath").toString().trimmed();
    if (!ext.isEmpty()) return ext;
    return defaultJsonPath();
}

QString ConfigTabs::currentJsPath() const {
    QSettings s;
    const QString ext = s.value("Config/ExternalCameraScriptPath").toString().trimmed();
    if (!ext.isEmpty()) return ext;
    return defaultJsPath();
}

void ConfigTabs::clearJsonSyncIndicators()
{
    if (jsonUnsavedLabel_) {
        jsonUnsavedLabel_->setVisible(false);
        jsonUnsavedLabel_->setText(tr("Unsaved changes – click Save to apply."));
    }
    if (jsonConflictLabel_) {
        jsonConflictLabel_->setVisible(false);
    }
}

bool ConfigTabs::loadFileToEditor(const QString& path, QPlainTextEdit* editor, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (err) *err = f.errorString();
        return false;
    }
    QTextStream in(&f);
    const bool blocked = editor->blockSignals(true);
    editor->setPlainText(in.readAll());
    editor->blockSignals(blocked);
    if (editor == jsonEdit_) {
        clearJsonSyncIndicators();
    }
    if (editor == jsEdit_ && jsUnsavedLabel_) jsUnsavedLabel_->setVisible(false);
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
    const QString path = currentJsonPath();
    if (path == defaultJsonPath()) {
        QString err;
        if (!ensureDefaultsFile(path, ":/defaults/config.json", &err)) {
            SPDLOG_WARN("ensureDefaultsFile(config.json) failed: {}", err.toStdString());
        }
    }
    QString err;
    if (!loadFileToEditor(path, jsonEdit_, &err)) {
        SPDLOG_WARN("Failed to load config.json from {}: {}", path.toStdString(), err.toStdString());
        QMessageBox::warning(this, tr("Reset config.json"), tr("Failed to load: %1").arg(err));
        return;
    }
    jsonPathLabel_->setText(path);
    clearJsonSyncIndicators();
    if (jsonStack_ && jsonStack_->currentIndex() == 1) {
        refreshJsonTableModel();
    }
}

void ConfigTabs::onSaveJson() {
    const QString path = currentJsonPath();
    QString err;
    if (!saveEditorToFile(jsonEdit_, path, &err)) {
        SPDLOG_ERROR("Failed to save config.json to {}: {}", path.toStdString(), err.toStdString());
        QMessageBox::warning(this, tr("Save config.json"), tr("Failed to save: %1").arg(err));
        return;
    }
    QMessageBox::information(this, tr("Save config.json"), tr("Saved."));
    clearJsonSyncIndicators();
    if (jsonStack_ && jsonStack_->currentIndex() == 1) {
        refreshJsonTableModel();
    }
}

void ConfigTabs::onReloadJs() {
    const QString path = currentJsPath();
    if (path == defaultJsPath()) {
        QString err;
        if (!ensureDefaultsFile(path, ":/defaults/egrabberConfig.js", &err)) {
            SPDLOG_WARN("ensureDefaultsFile(egrabberConfig.js) failed: {}", err.toStdString());
        }
    }
    QString err;
    if (!loadFileToEditor(path, jsEdit_, &err)) {
        SPDLOG_WARN("Failed to load egrabberConfig.js from {}: {}", path.toStdString(), err.toStdString());
        QMessageBox::warning(this, tr("Reset egrabberConfig.js"), tr("Failed to load: %1").arg(err));
        return;
    }
    jsPathLabel_->setText(path);
    if (jsUnsavedLabel_) jsUnsavedLabel_->setVisible(false);
}

void ConfigTabs::onSaveJs() {
    const QString path = currentJsPath();
    QString err;
    if (!saveEditorToFile(jsEdit_, path, &err)) {
        SPDLOG_ERROR("Failed to save egrabberConfig.js to {}: {}", path.toStdString(), err.toStdString());
        QMessageBox::warning(this, tr("Save egrabberConfig.js"), tr("Failed to save: %1").arg(err));
        return;
    }
    QMessageBox::information(this, tr("Save egrabberConfig.js"), tr("Saved."));
    if (jsUnsavedLabel_) jsUnsavedLabel_->setVisible(false);
}

void ConfigTabs::onResetCamera() {
    const auto reply = QMessageBox::question(
        this,
        tr("Reset Camera"),
        tr("This will send GenICam DeviceReset to the selected camera.\n\n"
           "Capture will be stopped and the camera may briefly disconnect.\n"
           "Proceed?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    if (jsResetBtn_) jsResetBtn_->setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);

    std::string backendErr;
    const bool ok = backend_.resetSelectedHardwareCamera(&backendErr);

    QApplication::restoreOverrideCursor();
    if (jsResetBtn_) jsResetBtn_->setEnabled(true);

    if (!ok) {
        SPDLOG_ERROR("Reset Camera failed: {}", backendErr);
        QMessageBox::warning(this, tr("Reset Camera"),
                             tr("Reset failed: %1").arg(QString::fromStdString(backendErr)));
        return;
    }

    QMessageBox::information(
        this,
        tr("Reset Camera"),
        tr("DeviceReset sent. The camera may briefly disconnect and require Refresh in the Connect tab."));
}

void ConfigTabs::onApplyJs() {
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

void ConfigTabs::onBrowseJson() {
    const QString current = currentJsonPath();
    const QString initialDir = QFileInfo(current).absolutePath();
    const QString selected = QFileDialog::getOpenFileName(this,
                                                          tr("Select App config (config.json)"),
                                                          initialDir,
                                                          tr("JSON files (*.json);;All Files (*.*)"));
    if (selected.isEmpty()) return;
    {
        QSettings s;
        s.setValue("Config/ExternalAppConfigPath", selected);
    }
    SPDLOG_INFO("External App config set to {}", selected.toStdString());
	emit appConfigPathChanged(selected);
    QString err;
    if (!loadFileToEditor(selected, jsonEdit_, &err)) {
        SPDLOG_WARN("Failed to load external config.json from {}: {}", selected.toStdString(), err.toStdString());
        QMessageBox::warning(this, tr("Reset config.json"), tr("Failed to load: %1").arg(err));
        return;
    }
    jsonPathLabel_->setText(selected);
    clearJsonSyncIndicators();
    if (jsonStack_ && jsonStack_->currentIndex() == 1) {
        refreshJsonTableModel();
    }
}

void ConfigTabs::onClearJson() {
    QSettings s;
    s.remove("Config/ExternalAppConfigPath");
    SPDLOG_INFO("External App config cleared; reverting to default include path");
	emit appConfigPathChanged(currentJsonPath());
    const auto ret = QMessageBox::question(this,
                                           tr("Config Path Cleared"),
                                           tr("External App config path cleared.\nReset from default include path now?\n\nNote: Save to apply any changes."),
                                           QMessageBox::Yes | QMessageBox::No,
                                           QMessageBox::Yes);
    if (ret == QMessageBox::Yes) {
        onReloadJson();
    }
}

void ConfigTabs::onBrowseJs() {
    const QString current = currentJsPath();
    const QString initialDir = QFileInfo(current).absolutePath();
    const QString selected = QFileDialog::getOpenFileName(this,
                                                          tr("Select Camera script (egrabberConfig.js)"),
                                                          initialDir,
                                                          tr("JavaScript files (*.js);;All Files (*.*)"));
    if (selected.isEmpty()) return;
    {
        QSettings s;
        s.setValue("Config/ExternalCameraScriptPath", selected);
    }
    SPDLOG_INFO("External Camera script set to {}", selected.toStdString());
    QString err;
    if (!loadFileToEditor(selected, jsEdit_, &err)) {
        SPDLOG_WARN("Failed to load external egrabberConfig.js from {}: {}", selected.toStdString(), err.toStdString());
        QMessageBox::warning(this, tr("Reset egrabberConfig.js"), tr("Failed to load: %1").arg(err));
        return;
    }
    jsPathLabel_->setText(selected);
    if (jsUnsavedLabel_) jsUnsavedLabel_->setVisible(false);
}

void ConfigTabs::onClearJs() {
    QSettings s;
    s.remove("Config/ExternalCameraScriptPath");
    SPDLOG_INFO("External Camera script cleared; reverting to default include path");
    const auto ret = QMessageBox::question(this,
                                           tr("Camera Script Path Cleared"),
                                           tr("External Camera script path cleared.\nReset from default include path now?\n\nNote: Save to apply any changes."),
                                           QMessageBox::Yes | QMessageBox::No,
                                           QMessageBox::Yes);
    if (ret == QMessageBox::Yes) {
        onReloadJs();
    }
}

void ConfigTabs::onJsonTableToggled(bool checked) {
    if (!jsonStack_) return;
    jsonStack_->setCurrentIndex(checked ? 1 : 0);
    QSettings s;
    s.setValue("Preview/ShowTable", checked);
    if (checked) {
        refreshJsonTableModel();
    }
}

void ConfigTabs::onJsonTextChangedDebounced() {
    if (jsonStack_ && jsonStack_->currentIndex() == 1) {
        refreshJsonTableModel();
    }
}

void ConfigTabs::refreshJsonTableModel() {
    if (!jsonModel_ || !jsonGridLayout_) return;
    const QByteArray bytes = jsonEdit_ ? jsonEdit_->toPlainText().toUtf8() : QByteArray();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError) {
        SPDLOG_WARN("JSON parse error at offset {}: {}", err.offset, err.errorString().toStdString());
        jsonModel_->clear();
        // Clear all section tables
        for (auto* model : jsonSectionModels_) {
            if (model) model->clear();
        }
        return;
    }
    
    // Use grouped layout
    const auto grouped = jsonutil::groupJsonBySections(doc);
    
    // Get list of current section names
    QStringList currentSections = jsonSectionTables_.keys();
    QStringList newSections = grouped.keys();
    
    // Remove sections that no longer exist
    for (const QString& sectionName : currentSections) {
        if (!newSections.contains(sectionName)) {
            QTableView* table = jsonSectionTables_.take(sectionName);
            JsonTableModel* model = jsonSectionModels_.take(sectionName);
            if (table && jsonGridLayout_) {
                // Find and remove group box (contains title + table)
                QString safe = sectionName;
                safe.replace(QRegularExpression(QString("[^A-Za-z0-9_]")), QString("_"));
                const QString groupName = QString("group_%1").arg(safe);
                if (auto* group = jsonGridContainer_->findChild<QGroupBox*>(groupName)) {
                    jsonGridLayout_->removeWidget(group);
                    group->deleteLater();
                } else {
                    // Fallback: ensure the table is removed if it was previously added directly
                    jsonGridLayout_->removeWidget(table);
                    table->deleteLater();
                }
            }
            if (model) {
                model->deleteLater();
            }
        }
    }
    
    // Clear grid layout (but keep widgets)
    if (jsonGridLayout_) {
        while (jsonGridLayout_->count() > 0) {
            QLayoutItem* item = jsonGridLayout_->takeAt(0);
            if (item && item->widget()) {
                item->widget()->hide();
            }
            delete item;
        }
    }
    
    // Update or create section tables and add to grid
    const int colsPerRow = 3;
    QStringList sortedSections = grouped.keys();
    sortedSections.sort(Qt::CaseInsensitive);
    
    int gridRow = 0;
    int gridCol = 0;
    
    for (const QString& sectionName : sortedSections) {
        const jsonutil::FlattenTable& sectionTable = grouped[sectionName];
        
        QTableView* table = jsonSectionTables_.value(sectionName, nullptr);
        JsonTableModel* model = jsonSectionModels_.value(sectionName, nullptr);

        // Stable object name for reusing widgets between refreshes
        QString safe = sectionName;
        safe.replace(QRegularExpression(QString("[^A-Za-z0-9_]")), QString("_"));
        const QString groupName = QString("group_%1").arg(safe);
        QGroupBox* group = jsonGridContainer_->findChild<QGroupBox*>(groupName);
        
        if (!group) {
            group = new QGroupBox(sectionName, jsonGridContainer_);
            group->setObjectName(groupName);
            group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            auto* gv = new QVBoxLayout(group);
            gv->setContentsMargins(6, 6, 6, 6);
            gv->setSpacing(4);

            // Create new table + model
            model = new JsonTableModel(this);
            table = new QTableView(group);
            table->setModel(model);
            table->setSelectionBehavior(QAbstractItemView::SelectRows);
            table->setSelectionMode(QAbstractItemView::SingleSelection);
            table->setAlternatingRowColors(true);
            table->setSortingEnabled(false);
            table->setWordWrap(true);
            table->setTextElideMode(Qt::ElideNone);
            table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
            table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
            table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            table->setMinimumHeight(140);

            gv->addWidget(table, 1);

            // Connect dataChanged signal
            connect(model, &QAbstractItemModel::dataChanged, this,
                    [this](const QModelIndex&, const QModelIndex&, const QVector<int>&) { rebuildJsonFromTable(); });

            jsonSectionTables_[sectionName] = table;
            jsonSectionModels_[sectionName] = model;
        } else if (!table) {
            // Group exists from a previous refresh; recover the table/model pointers
            table = group->findChild<QTableView*>();
            model = table ? qobject_cast<JsonTableModel*>(table->model()) : nullptr;
            if (table) jsonSectionTables_[sectionName] = table;
            if (model) jsonSectionModels_[sectionName] = model;
        }
        
        // Update model data
        if (model) {
            model->setFromFlatten(sectionTable.columns, sectionTable.rows);
        }

        if (group) group->show();
        if (table) table->show();

        // Add grouped widget to grid (keeps header + body together)
        jsonGridLayout_->addWidget(group, gridRow, gridCol, 1, 1, Qt::AlignTop);
        
        // Resize columns
        if (table) {
            table->resizeColumnsToContents();
            table->resizeRowsToContents();
        }
        
        // Move to next column, wrap to next row if needed
        gridCol++;
        if (gridCol >= colsPerRow) {
            gridCol = 0;
            gridRow++;
        }
    }

    // Make rows expand from the top to use available space nicely
    for (int r = 0; r <= gridRow; ++r) {
        jsonGridLayout_->setRowStretch(r, 1);
    }
    
    // Also update legacy single table for backward compatibility
    const auto flattened = jsonutil::flattenJsonForTable(doc);
    jsonModel_->setFromFlatten(flattened.columns, flattened.rows);
    if (jsonTable_) {
        jsonTable_->resizeColumnsToContents();
		jsonTable_->resizeRowsToContents();
    }
}

void ConfigTabs::onExternalConfigFileChanged(const QString& path) {
    // Only reload if the changed file matches the current JSON path
    if (path != currentJsonPath()) {
        return;
    }

    // Only reload if there are no unsaved changes to avoid overwriting user edits.
    if (jsonUnsavedLabel_ && jsonUnsavedLabel_->isVisible()) {
        if (jsonConflictLabel_) {
            jsonConflictLabel_->setVisible(true);
        }
        SPDLOG_WARN("ConfigTabs: config changed externally while editor has unsaved changes");
        return;
    }

    // Reload the file into the editor
    QString err;
    if (!loadFileToEditor(path, jsonEdit_, &err)) {
        SPDLOG_WARN("ConfigTabs: failed to reload config.json from {}: {}", path.toStdString(), err.toStdString());
        return;
    }

    // Refresh the JSON table if it's visible
    if (jsonStack_ && jsonStack_->currentIndex() == 1) {
        refreshJsonTableModel();
    }

    SPDLOG_DEBUG("ConfigTabs: reloaded config.json from external change");
}

void ConfigTabs::rebuildJsonFromTable() {
	if (!jsonEdit_) return;

	QMap<QString, jsonutil::FlattenTable> sections;

	// Collect data from all section tables
	for (auto it = jsonSectionModels_.constBegin(); it != jsonSectionModels_.constEnd(); ++it) {
		const QString& sectionName = it.key();
		JsonTableModel* model = it.value();
		if (!model) continue;
		
		const auto& cols = model->columns();
		const auto& rows = model->rows();
		sections.insert(sectionName, jsonutil::FlattenTable{cols, rows});
	}

	if (sections.isEmpty() && jsonModel_) {
		sections.insert(QStringLiteral("General"),
		                jsonutil::FlattenTable{jsonModel_->columns(), jsonModel_->rows()});
	}

	const QJsonDocument outDoc = jsonutil::rebuildJsonDocumentFromSections(sections);
	if (outDoc.isNull()) {
		return;
	}

	// Update editor without triggering table refresh loop
	const bool blocked = jsonEdit_->blockSignals(true);
	jsonEdit_->setPlainText(QString::fromUtf8(outDoc.toJson(QJsonDocument::Indented)));
	jsonEdit_->blockSignals(blocked);
	if (jsonUnsavedLabel_) jsonUnsavedLabel_->setVisible(true);
}

// ===== Profiles helpers =====
QString ConfigTabs::profilesBaseDir() const {
    return profileManager_.profilesBaseDir();
}

bool ConfigTabs::ensureProfilesDirExists(QString* err) const {
    QDir dir(profileManager_.profilesBaseDir());
    if (dir.exists()) return true;
    if (!dir.mkpath(".")) {
        if (err) *err = tr("Failed to create profiles dir: %1").arg(dir.absolutePath());
        return false;
    }
    return true;
}

QStringList ConfigTabs::listProfiles() const {
    QStringList result;
    QString err;
    const auto summaries = profileManager_.scanLocalProfiles(true, remoteCatalog_ ? &*remoteCatalog_ : nullptr, &err);
    if (!err.isEmpty()) {
        SPDLOG_WARN("ConfigTabs: failed to scan profiles for list: {}", err.toStdString());
    }
    for (const auto& summary : summaries) {
        result << summary.profileName;
    }
    return result;
}

void ConfigTabs::refreshProfilesList() {
    if (!profileSelect_) return;
    const QString last = QSettings().value("Profiles/LastProfileName").toString();
    QString err;
    const auto summaries = profileManager_.scanLocalProfiles(true, remoteCatalog_ ? &*remoteCatalog_ : nullptr, &err);
    if (!err.isEmpty()) {
        SPDLOG_WARN("ConfigTabs: profile scan warning: {}", err.toStdString());
    }
    const bool blocked = profileSelect_->blockSignals(true);
    profileSelect_->clear();
    profileSelect_->addItem(tr("<no profile>"), "");
    for (const auto& summary : summaries) {
        profileSelect_->addItem(profileLabelForSummary(summary), summary.profileName);
    }
    int idx = profileSelect_->findData(last);
    if (idx < 0) idx = 0;
    profileSelect_->setCurrentIndex(idx);
    profileSelect_->blockSignals(blocked);
    if (idx > 0) {
        onProfileSelectionChanged(idx);
    } else {
        refreshProfileStatusLabel();
    }
}

QString ConfigTabs::sanitizeProfileName(const QString& name) const {
    QString n = name.trimmed();
    // Remove invalid chars for folder names on Windows and *nix
    n.replace(QRegularExpression(QString("[\\\\/:*?\"<>|]")), QString("_"));
    if (n == "." || n == "..") n = "profile";
    if (n.isEmpty()) n = "profile";
    if (n.size() > 64) n = n.left(64);
    return n;
}

bool ConfigTabs::writeTextFile(const QString& path, const QString& content, QString* err) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (err) *err = f.errorString();
        return false;
    }
    QTextStream out(&f);
    out << content;
    return true;
}

bool ConfigTabs::readTextFile(const QString& path, QString* out, QString* err) const {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (err) *err = f.errorString();
        return false;
    }
    QTextStream in(&f);
    *out = in.readAll();
    return true;
}

QString ConfigTabs::profileDirPath(const QString& profileName) const {
    return QDir(profilesBaseDir()).absoluteFilePath(profileName);
}
QString ConfigTabs::profileJsonPath(const QString& profileName) const {
    return QDir(profileDirPath(profileName)).absoluteFilePath("config.json");
}
QString ConfigTabs::profileJsPath(const QString& profileName) const {
    return QDir(profileDirPath(profileName)).absoluteFilePath("egrabberConfig.js");
}

QString ConfigTabs::selectedProfileName() const {
    if (!profileSelect_) {
        return QString();
    }
    return profileSelect_->currentData().toString();
}

std::optional<frontend::ProfileManager::LocalProfile> ConfigTabs::selectedProfileSummary() const {
    const QString name = selectedProfileName();
    if (name.isEmpty()) {
        return std::nullopt;
    }
    QString err;
    const auto summaries = profileManager_.scanLocalProfiles(true, remoteCatalog_ ? &*remoteCatalog_ : nullptr, &err);
    if (!err.isEmpty()) {
        SPDLOG_WARN("ConfigTabs: failed to fetch selected profile summary: {}", err.toStdString());
    }
    for (const auto& summary : summaries) {
        if (summary.profileName == name) {
            return summary;
        }
    }
    return std::nullopt;
}

std::optional<frontend::ProfileManager::CatalogEntry> ConfigTabs::selectedRemoteCatalogEntry() const {
    const auto summary = selectedProfileSummary();
    if (!summary.has_value() || !summary->remoteEntry.has_value()) {
        return std::nullopt;
    }
    return summary->remoteEntry;
}

QString ConfigTabs::profileLabelForSummary(const frontend::ProfileManager::LocalProfile& summary) const {
    QStringList tags;
    if (summary.remoteEntry.has_value()) {
        tags << tr("remote");
    } else {
        tags << tr("local");
    }
    if (summary.updateAvailable) {
        tags << tr("update available");
    }
    if (summary.dirty) {
        tags << tr("dirty");
    }
    if (summary.incompatible) {
        tags << tr("incompatible");
    }
    const QString base = summary.displayName.trimmed().isEmpty() ? summary.profileName : summary.displayName.trimmed();
    return tags.isEmpty() ? base : QStringLiteral("%1 [%2]").arg(base, tags.join(QStringLiteral(", ")));
}

void ConfigTabs::refreshProfileStatusLabel() {
    if (!profileStatusLabel_) {
        return;
    }
    const auto summary = selectedProfileSummary();
    if (!summary.has_value()) {
        profileStatusLabel_->setText(tr("No profile selected"));
        profileStatusLabel_->setToolTip(QString());
        return;
    }

    QStringList details;
    details << (summary->remoteEntry.has_value() ? tr("remote") : tr("local-only"));
    if (summary->updateAvailable) {
        details << tr("update available");
    }
    if (summary->dirty) {
        details << tr("dirty");
    }
    if (summary->incompatible) {
        details << tr("incompatible");
    }
    profileStatusLabel_->setText(details.join(QStringLiteral(" | ")));
    profileStatusLabel_->setToolTip(QStringLiteral("Profile: %1\nConfig: %2\nMetadata: %3")
                                        .arg(summary->profileName,
                                             summary->hasConfig ? summary->configPath : tr("missing"),
                                             summary->hasMetadata ? summary->metaPath : tr("missing")));
}

void ConfigTabs::showDiffDialog(const QString& title, const QVector<frontend::ProfileManager::DiffRow>& rows) {
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(1100, 640);
    auto* layout = new QVBoxLayout(&dialog);
    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({tr("Path"), tr("Status"), tr("Local value"), tr("Remote value"), tr("Risk"), tr("Source")});
    table->setRowCount(rows.size());
    table->setAlternatingRowColors(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);

    for (int i = 0; i < rows.size(); ++i) {
        const auto& row = rows.at(i);
        const QString status = frontend::ProfileManager::diffStatusToString(row.status);
        table->setItem(i, 0, new QTableWidgetItem(row.path));
        table->setItem(i, 1, new QTableWidgetItem(status));
        table->setItem(i, 2, new QTableWidgetItem(row.localValue));
        table->setItem(i, 3, new QTableWidgetItem(row.remoteValue));
        table->setItem(i, 4, new QTableWidgetItem(row.risk));
        table->setItem(i, 5, new QTableWidgetItem(row.source));
    }

    layout->addWidget(table, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void ConfigTabs::loadSelectedProfileInternal(const QString& profileName) {
    if (profileName.isEmpty()) return;
    const QString cfgPath = profileJsonPath(profileName);
    if (!QFile::exists(cfgPath)) {
        QMessageBox::warning(this, tr("Load Profile"), tr("Profile missing config.json: %1").arg(profileName));
        return;
    }
    // Set external paths and reload
    {
        QSettings s;
        s.setValue("Config/ExternalAppConfigPath", cfgPath);
        s.setValue("Profiles/LastProfileName", profileName);
    }
    SPDLOG_INFO("Profiles: loading profile '{}' (json: {})", profileName.toStdString(), cfgPath.toStdString());
    emit appConfigPathChanged(cfgPath);
    onReloadJson();
    // JS optional
    const bool includeJs = profilesIncludeJsCheck_ ? profilesIncludeJsCheck_->isChecked() : true;
    const QString jsPath = profileJsPath(profileName);
    if (includeJs && QFile::exists(jsPath)) {
        QSettings().setValue("Config/ExternalCameraScriptPath", jsPath);
        onReloadJs();
    }
    refreshProfileStatusLabel();
}

// ===== Profiles slots =====
void ConfigTabs::onProfileSelectionChanged(int index) {
    if (!profileSelect_) return;
    const QString profileName = profileSelect_->itemData(index).toString();
    if (profileName.isEmpty()) {
        // Switch back to default include path (no active profile)
        QSettings s;
        s.remove("Profiles/LastProfileName");
        s.remove("Config/ExternalAppConfigPath");
        s.remove("Config/ExternalCameraScriptPath");
        SPDLOG_INFO("Profiles: cleared active profile; reverting to default include paths");
        onReloadJson();
        onReloadJs();
        refreshProfileStatusLabel();
        return;
    }
    loadSelectedProfileInternal(profileName);
}

void ConfigTabs::onSaveProfile() {
    QString name = QInputDialog::getText(this, tr("Save Profile"),
                                         tr("Profile name:"), QLineEdit::Normal);
    if (name.isNull()) return; // cancelled
    name = sanitizeProfileName(name);
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Save Profile"), tr("Invalid profile name."));
        return;
    }
    QString err;
    if (!ensureProfilesDirExists(&err)) {
        QMessageBox::warning(this, tr("Save Profile"), err);
        return;
    }
    const QString dir = profileDirPath(name);
    QDir().mkpath(dir);
    const QString cfgPath = profileJsonPath(name);
    const QString jsPath = profileJsPath(name);
    const bool includeJs = profilesIncludeJsCheck_ ? profilesIncludeJsCheck_->isChecked() : true;

    // Prepare JSON (pretty) from editor; validate
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonEdit_ ? jsonEdit_->toPlainText().toUtf8() : QByteArray(), &perr);
    QString jsonToWrite;
    if (perr.error == QJsonParseError::NoError) {
        jsonToWrite = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    } else {
        const auto ret = QMessageBox::question(this, tr("Invalid JSON"),
                                               tr("JSON in editor is invalid.\nError: %1\n\nForce-save raw text to profile anyway?")
                                               .arg(perr.errorString()),
                                               QMessageBox::Yes | QMessageBox::No,
                                               QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
        jsonToWrite = jsonEdit_ ? jsonEdit_->toPlainText() : QString();
    }

    // Confirm overwrite if target exists
    if (QFile::exists(cfgPath)) {
        const auto ret = QMessageBox::question(this, tr("Overwrite"),
                                               tr("Profile already has config.json. Overwrite?"),
                                               QMessageBox::Yes | QMessageBox::No,
                                               QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    }
    if (!writeTextFile(cfgPath, jsonToWrite, &err)) {
        QMessageBox::warning(this, tr("Save Profile"), tr("Failed to write config.json: %1").arg(err));
        return;
    }

    if (includeJs) {
        const QString jsText = jsEdit_ ? jsEdit_->toPlainText() : QString();
        if (!jsText.isEmpty()) {
            if (QFile::exists(jsPath)) {
                const auto ret2 = QMessageBox::question(this, tr("Overwrite"),
                                                        tr("Profile already has egrabberConfig.js. Overwrite?"),
                                                        QMessageBox::Yes | QMessageBox::No,
                                                        QMessageBox::No);
                if (ret2 != QMessageBox::Yes) return;
            }
            if (!writeTextFile(jsPath, jsText, &err)) {
                QMessageBox::warning(this, tr("Save Profile"), tr("Failed to write egrabberConfig.js: %1").arg(err));
                return;
            }
        }
    }

    // Update QSettings, refresh list, select and load
    {
        QSettings s;
        s.setValue("Profiles/LastProfileName", name);
    }
    refreshProfilesList();
    const int idx = profileSelect_ ? profileSelect_->findData(name) : -1;
    if (idx >= 0 && profileSelect_->currentIndex() != idx) {
        profileSelect_->setCurrentIndex(idx); // will trigger load
    } else {
        loadSelectedProfileInternal(name);
    }
    SPDLOG_INFO("Profiles: saved profile '{}' (json={}, js_included={})",
                name.toStdString(), cfgPath.toStdString(), includeJs ? 1 : 0);
    refreshProfileStatusLabel();
}

void ConfigTabs::onDeleteProfile() {
    if (!profileSelect_ || profileSelect_->currentIndex() <= 0) {
        QMessageBox::information(this, tr("Delete Profile"), tr("No profile selected."));
        return;
    }
    const QString name = profileSelect_->currentData().toString();
    const auto ret = QMessageBox::question(this, tr("Delete Profile"),
                                           tr("Delete profile '%1'?\nThis cannot be undone.").arg(name),
                                           QMessageBox::Yes | QMessageBox::No,
                                           QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    // If active, revert to default before delete
    const QString activeJson = currentJsonPath();
    const QString nameJson = profileJsonPath(name);
    if (QFileInfo(activeJson).absoluteFilePath() == QFileInfo(nameJson).absoluteFilePath()) {
        QSettings s;
        s.remove("Config/ExternalAppConfigPath");
        s.remove("Config/ExternalCameraScriptPath");
        s.remove("Profiles/LastProfileName");
        SPDLOG_INFO("Profiles: deleting active profile, reverting to defaults");
        onReloadJson();
        onReloadJs();
    }
    QDir dir(profileDirPath(name));
    bool ok = dir.removeRecursively();
    if (!ok) {
        QMessageBox::warning(this, tr("Delete Profile"), tr("Failed to delete profile directory."));
        return;
    }
    SPDLOG_INFO("Profiles: deleted profile '{}'", name.toStdString());
    refreshProfilesList();
}

void ConfigTabs::onRenameProfile() {
    if (!profileSelect_ || profileSelect_->currentIndex() <= 0) {
        QMessageBox::information(this, tr("Rename Profile"), tr("No profile selected."));
        return;
    }
    const QString oldName = profileSelect_->currentData().toString();
    QString newName = QInputDialog::getText(this, tr("Rename Profile"),
                                            tr("New profile name:"), QLineEdit::Normal,
                                            oldName);
    if (newName.isNull()) return;
    newName = sanitizeProfileName(newName);
    if (newName == oldName) return;
    if (newName.isEmpty()) {
        QMessageBox::warning(this, tr("Rename Profile"), tr("Invalid profile name."));
        return;
    }
    const QString oldDir = profileDirPath(oldName);
    const QString newDir = profileDirPath(newName);
    if (QFile::exists(newDir)) {
        QMessageBox::warning(this, tr("Rename Profile"), tr("A profile with that name already exists."));
        return;
    }
    QDir base(profilesBaseDir());
    if (!base.rename(oldName, newName)) {
        QMessageBox::warning(this, tr("Rename Profile"), tr("Failed to rename profile directory."));
        return;
    }
    // If active, update settings
    const QString activeJson = currentJsonPath();
    const QString oldJson = profileJsonPath(oldName);
    if (QFileInfo(activeJson).absoluteFilePath() == QFileInfo(oldJson).absoluteFilePath()) {
        QSettings s;
        s.setValue("Config/ExternalAppConfigPath", profileJsonPath(newName));
        const bool includeJs = profilesIncludeJsCheck_ ? profilesIncludeJsCheck_->isChecked() : true;
        if (includeJs && QFile::exists(profileJsPath(newName))) {
            s.setValue("Config/ExternalCameraScriptPath", profileJsPath(newName));
        }
        s.setValue("Profiles/LastProfileName", newName);
        SPDLOG_INFO("Profiles: active profile renamed to '{}'", newName.toStdString());
        onReloadJson();
        onReloadJs();
    }
    refreshProfilesList();
    const int idx = profileSelect_->findData(newName);
    if (idx >= 0) profileSelect_->setCurrentIndex(idx);
    refreshProfileStatusLabel();
}

void ConfigTabs::onCheckProfileUpdates() {
    const QUrl catalogUrl = profileManager_.catalogUrlFromEnvOrDefault(QStringLiteral("stable"));
    QString err;
    const auto catalog = profileManager_.fetchCatalog(catalogUrl, &err);
    if (!catalog.has_value()) {
        QMessageBox::warning(this, tr("Check Updates"), tr("Failed to fetch catalog:\n%1").arg(err));
        SPDLOG_WARN("ConfigTabs: catalog fetch failed from {}: {}", catalogUrl.toString().toStdString(), err.toStdString());
        return;
    }
    if (catalog->catalogSchemaVersion <= 0) {
        QMessageBox::warning(this, tr("Check Updates"), tr("Catalog is missing catalog_schema_version."));
        return;
    }

    remoteCatalog_ = catalog;
    refreshProfilesList();

    const int remoteCount = catalog->profiles.size();
    int updateCount = 0;
    const auto refreshed = profileManager_.scanLocalProfiles(true, &*remoteCatalog_, nullptr);
    for (const auto& profile : refreshed) {
        if (profile.updateAvailable) {
            ++updateCount;
        }
    }
    QMessageBox::information(this,
                             tr("Check Updates"),
                             tr("Catalog refreshed from %1.\n\nProfiles in catalog: %2\nProfiles with updates: %3")
                                 .arg(catalogUrl.toString())
                                 .arg(remoteCount)
                                 .arg(updateCount));
}

void ConfigTabs::onUpdateSelectedProfile() {
    const auto selected = selectedProfileSummary();
    if (!selected.has_value()) {
        QMessageBox::information(this, tr("Update Selected"), tr("No profile selected."));
        return;
    }
    if (!selected->remoteEntry.has_value()) {
        QMessageBox::information(this, tr("Update Selected"), tr("The selected profile does not have remote catalog data."));
        return;
    }
    const auto ret = QMessageBox::question(this,
                                           tr("Update Selected"),
                                           tr("Update profile '%1' from the public catalog?\n\nThis will replace config.json and any matching camera script after checksum verification.").arg(selected->profileName),
                                           QMessageBox::Yes | QMessageBox::No,
                                           QMessageBox::Yes);
    if (ret != QMessageBox::Yes) {
        return;
    }

    QString err;
    if (!profileManager_.installRemoteProfile(*selected->remoteEntry, selected->profileName, &err)) {
        QMessageBox::warning(this, tr("Update Selected"), tr("Failed to install profile update:\n%1").arg(err));
        SPDLOG_WARN("ConfigTabs: failed to install remote profile '{}': {}", selected->profileName.toStdString(), err.toStdString());
        return;
    }

    refreshProfilesList();
    const QString cfgPath = profileJsonPath(selected->profileName);
    const bool active = QFileInfo(currentJsonPath()).absoluteFilePath() == QFileInfo(cfgPath).absoluteFilePath();
    if (active) {
        emit appConfigPathChanged(cfgPath);
        onReloadJson();
        const bool includeJs = profilesIncludeJsCheck_ ? profilesIncludeJsCheck_->isChecked() : true;
        if (includeJs && QFile::exists(profileJsPath(selected->profileName))) {
            QSettings().setValue("Config/ExternalCameraScriptPath", profileJsPath(selected->profileName));
            onReloadJs();
        }
    }
    refreshProfileStatusLabel();
    QMessageBox::information(this, tr("Update Selected"), tr("Profile update installed successfully."));
}

void ConfigTabs::onShowProfileDiff() {
    const auto selected = selectedProfileSummary();
    if (!selected.has_value()) {
        QMessageBox::information(this, tr("Show Diff"), tr("No profile selected."));
        return;
    }
    if (!remoteCatalog_.has_value() || !selected->remoteEntry.has_value()) {
        QMessageBox::information(this, tr("Show Diff"), tr("Fetch the public catalog first, then select a remote-managed profile."));
        return;
    }

    QByteArray remoteConfig;
    QString remoteErr;
    if (!profileManager_.downloadUrlBlocking(selected->remoteEntry->configUrl, &remoteConfig, &remoteErr)) {
        QMessageBox::warning(this, tr("Show Diff"), tr("Failed to download remote config:\n%1").arg(remoteErr));
        return;
    }

    QString localErr;
    const auto localBytes = profileManager_.readFileBytes(profileJsonPath(selected->profileName), &localErr);
    if (!localBytes.has_value()) {
        QMessageBox::warning(this, tr("Show Diff"), tr("Failed to read local config:\n%1").arg(localErr));
        return;
    }

    QString diffErr;
    const auto rows = profileManager_.diffConfigBytes(*localBytes, remoteConfig, &diffErr);
    if (!diffErr.isEmpty()) {
        QMessageBox::warning(this, tr("Show Diff"), tr("Failed to diff configs:\n%1").arg(diffErr));
        return;
    }
    if (rows.isEmpty()) {
        QMessageBox::information(this, tr("Show Diff"), tr("No configuration differences detected."));
        return;
    }
    showDiffDialog(tr("Profile Diff: %1").arg(selected->profileName), rows);
}

void ConfigTabs::onDuplicateProfileAsLocal() {
    const auto selected = selectedProfileSummary();
    if (!selected.has_value()) {
        QMessageBox::information(this, tr("Duplicate as Local"), tr("No profile selected."));
        return;
    }
    QString newName = QInputDialog::getText(this,
                                            tr("Duplicate as Local"),
                                            tr("New local profile name:"),
                                            QLineEdit::Normal,
                                            selected->profileName + QStringLiteral("-copy"));
    if (newName.isNull()) {
        return;
    }
    newName = sanitizeProfileName(newName);
    if (newName.isEmpty()) {
        QMessageBox::warning(this, tr("Duplicate as Local"), tr("Invalid profile name."));
        return;
    }

    QString err;
    if (!profileManager_.duplicateProfileAsLocal(selected->profileName, newName, &err)) {
        QMessageBox::warning(this, tr("Duplicate as Local"), tr("Failed to duplicate profile:\n%1").arg(err));
        return;
    }

    refreshProfilesList();
    const int idx = profileSelect_ ? profileSelect_->findData(newName) : -1;
    if (idx >= 0) {
        profileSelect_->setCurrentIndex(idx);
    }
    QMessageBox::information(this, tr("Duplicate as Local"), tr("Created local profile '%1'.").arg(newName));
}

void ConfigTabs::onIncludeJsToggled(bool checked) {
    QSettings().setValue("Profiles/IncludeJs", checked);
}

} // namespace frontend
