#include "frontend/ConfigTabs.h"

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

#include <spdlog/spdlog.h>

#include "backend/AppBackend.h"
#include "frontend/JsonTableModel.h"
#include "frontend/JsonFlatten.h"
#include "frontend/NanopositionerTab.h"

namespace frontend {

namespace {

static QJsonValue parseValueFromString(const QString& text) {
	const QString t = text.trimmed();
	if (t == "true") return QJsonValue(true);
	if (t == "false") return QJsonValue(false);
	if (t == "null" || t == "undefined") return QJsonValue();
	// Try number
	bool ok = false;
	double d = t.toDouble(&ok);
	if (ok && !t.isEmpty()) return QJsonValue(d);
	// Try array/object JSON
	if ((!t.isEmpty() && (t.front() == '{' || t.front() == '['))) {
		QJsonParseError err;
		const QJsonDocument doc = QJsonDocument::fromJson(t.toUtf8(), &err);
		if (err.error == QJsonParseError::NoError) {
			if (doc.isObject()) return QJsonValue(doc.object());
			if (doc.isArray()) return QJsonValue(doc.array());
		}
	}
	return QJsonValue(t);
}

static void setObjectValueByPath(QJsonObject& obj, const QString& path, const QJsonValue& value) {
	const QStringList parts = path.split('.', Qt::SkipEmptyParts);
	std::function<void(QJsonObject&, int)> setByIndex = [&](QJsonObject& node, int idx) {
		if (idx >= parts.size()) return;
		const QString& key = parts.at(idx);
		if (idx == parts.size() - 1) {
			node.insert(key, value);
			return;
		}
		QJsonObject child = node.value(key).toObject();
		setByIndex(child, idx + 1);
		node.insert(key, child);
	};
	setByIndex(obj, 0);
}

} // namespace

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
        jsonReloadBtn_ = new QPushButton(tr("Reset"), page);
        jsonSaveBtn_ = new QPushButton(tr("Save"), page);
        jsonBrowseBtn_ = new QPushButton(tr("Browse..."), page);
        jsonClearBtn_ = new QPushButton(tr("Clear"), page);
		jsonTableToggle_ = new QToolButton(page);
		jsonTableToggle_->setText(tr("json/table"));
        jsonTableToggle_->setToolTip(tr("Toggle table view"));
        jsonTableToggle_->setCheckable(true);
        jsonPathLabel_ = new QLabel(page);
        jsonUnsavedLabel_ = new QLabel(page);
        jsonUnsavedLabel_->setText(tr("Unsaved changes – click Save to apply."));
        jsonUnsavedLabel_->setVisible(false);
        jsonUnsavedLabel_->setStyleSheet("color: #d17a00;");
        profileSelect_ = new QComboBox(page);
        saveProfileBtn_ = new QPushButton(tr("Save Profile"), page);
        deleteProfileBtn_ = new QPushButton(tr("Delete"), page);
        renameProfileBtn_ = new QPushButton(tr("Rename"), page);
        row->addWidget(jsonReloadBtn_);
        row->addWidget(jsonSaveBtn_);
        row->addWidget(jsonBrowseBtn_);
        row->addWidget(jsonClearBtn_);
		row->addStretch(1);
		row->addWidget(jsonPathLabel_);
        row->addSpacing(8);
        row->addWidget(jsonUnsavedLabel_);
        row->addSpacing(8);
        row->addWidget(new QLabel(tr("Profile:"), page));
        row->addWidget(profileSelect_);
        row->addWidget(saveProfileBtn_);
        row->addWidget(renameProfileBtn_);
        row->addWidget(deleteProfileBtn_);
		row->addWidget(jsonTableToggle_);
        v->addLayout(row);

        jsonModel_ = new JsonTableModel(this);
        jsonTable_ = new QTableView(page);
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
        jsonStack_->addWidget(jsonEdit_);
        jsonStack_->addWidget(jsonTable_);
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
        v->addLayout(row);
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

    // Nanopositioner tab
    {
        auto* nanoTab = new frontend::NanopositionerTab(backend_, this);
        tabs_->addTab(nanoTab, tr("Nanopositioner"));
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
}

QString ConfigTabs::appDirIncludePath(const QString& fileName) const {
    const QString appDir = QCoreApplication::applicationDirPath();
    // Write into a sibling include directory next to the executable dir (build tree)
    return QDir(appDir).absoluteFilePath("../include/" + fileName);
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
    if (editor == jsonEdit_ && jsonUnsavedLabel_) jsonUnsavedLabel_->setVisible(false);
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
    if (jsonUnsavedLabel_) jsonUnsavedLabel_->setVisible(false);
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
    if (jsonUnsavedLabel_) jsonUnsavedLabel_->setVisible(false);
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
    if (jsonUnsavedLabel_) jsonUnsavedLabel_->setVisible(false);
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
    if (!jsonModel_) return;
    const QByteArray bytes = jsonEdit_ ? jsonEdit_->toPlainText().toUtf8() : QByteArray();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError) {
        SPDLOG_WARN("JSON parse error at offset {}: {}", err.offset, err.errorString().toStdString());
        jsonModel_->clear();
        return;
    }
    const auto flattened = jsonutil::flattenJsonForTable(doc);
    jsonModel_->setFromFlatten(flattened.columns, flattened.rows);
    if (jsonTable_) {
        jsonTable_->resizeColumnsToContents();
		jsonTable_->resizeRowsToContents();
    }
}

void ConfigTabs::rebuildJsonFromTable() {
	if (!jsonModel_ || !jsonEdit_) return;
	const auto& cols = jsonModel_->columns();
	const auto& rows = jsonModel_->rows();
	QJsonDocument outDoc;
	if (cols.size() == 2 && cols.at(0) == "key" && cols.at(1) == "value") {
		QJsonObject root;
		for (const auto& r : rows) {
			if (r.size() < 2) continue;
			const QString keyPath = r.at(0);
			const QString valStr = r.at(1);
			setObjectValueByPath(root, keyPath, parseValueFromString(valStr));
		}
		outDoc = QJsonDocument(root);
	} else {
		// Treat as array of objects
		QJsonArray arr;
		for (const auto& r : rows) {
			QJsonObject obj;
			for (int c = 0; c < cols.size() && c < r.size(); ++c) {
				const QString keyPath = cols.at(c);
				const QString valStr = r.at(c);
				setObjectValueByPath(obj, keyPath, parseValueFromString(valStr));
			}
			arr.append(obj);
		}
		outDoc = QJsonDocument(arr);
	}
	// Update editor without triggering table refresh loop
	const bool blocked = jsonEdit_->blockSignals(true);
	jsonEdit_->setPlainText(QString::fromUtf8(outDoc.toJson(QJsonDocument::Indented)));
	jsonEdit_->blockSignals(blocked);
	if (jsonUnsavedLabel_) jsonUnsavedLabel_->setVisible(true);
}

// ===== Profiles helpers =====
QString ConfigTabs::profilesBaseDir() const {
    const QString appDir = QCoreApplication::applicationDirPath();
    return QDir(appDir).absoluteFilePath("../include/profiles");
}

bool ConfigTabs::ensureProfilesDirExists(QString* err) const {
    QDir dir(profilesBaseDir());
    if (dir.exists()) return true;
    if (!dir.mkpath(".")) {
        if (err) *err = tr("Failed to create profiles dir: %1").arg(dir.absolutePath());
        return false;
    }
    return true;
}

QStringList ConfigTabs::listProfiles() const {
    QStringList result;
    QDir dir(profilesBaseDir());
    if (!dir.exists()) return result;
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : entries) {
        const QString cfg = QDir(fi.absoluteFilePath()).absoluteFilePath("config.json");
        if (QFile::exists(cfg)) {
            result << fi.fileName();
        }
    }
    return result;
}

void ConfigTabs::refreshProfilesList() {
    if (!profileSelect_) return;
    const QString last = QSettings().value("Profiles/LastProfileName").toString();
    const QStringList profiles = listProfiles();
    const bool blocked = profileSelect_->blockSignals(true);
    profileSelect_->clear();
    profileSelect_->addItem(tr("<no profile>"), "");
    for (const QString& p : profiles) {
        profileSelect_->addItem(p, p);
    }
    int idx = profileSelect_->findData(last);
    if (idx < 0) idx = 0;
    profileSelect_->setCurrentIndex(idx);
    profileSelect_->blockSignals(blocked);
    // Auto-load if not <no profile>
    if (idx > 0) {
        onProfileSelectionChanged(idx);
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
}

void ConfigTabs::onIncludeJsToggled(bool checked) {
    QSettings().setValue("Profiles/IncludeJs", checked);
}

} // namespace frontend



