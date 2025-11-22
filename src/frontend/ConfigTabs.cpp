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
        jsonReloadBtn_ = new QPushButton(tr("Reload"), page);
        jsonSaveBtn_ = new QPushButton(tr("Save"), page);
        jsonBrowseBtn_ = new QPushButton(tr("Browse..."), page);
        jsonClearBtn_ = new QPushButton(tr("Clear"), page);
		jsonTableToggle_ = new QToolButton(page);
		jsonTableToggle_->setText(tr("json/table"));
        jsonTableToggle_->setToolTip(tr("Toggle table view"));
        jsonTableToggle_->setCheckable(true);
        jsonPathLabel_ = new QLabel(page);
        row->addWidget(jsonReloadBtn_);
        row->addWidget(jsonSaveBtn_);
        row->addWidget(jsonBrowseBtn_);
        row->addWidget(jsonClearBtn_);
		row->addStretch(1);
		row->addWidget(jsonPathLabel_);
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
        jsReloadBtn_ = new QPushButton(tr("Reload"), page);
        jsSaveBtn_ = new QPushButton(tr("Save"), page);
        jsApplyBtn_ = new QPushButton(tr("Apply to Camera"), page);
        jsResetBtn_ = new QPushButton(tr("Reset Camera"), page);
        jsBrowseBtn_ = new QPushButton(tr("Browse..."), page);
        jsClearBtn_ = new QPushButton(tr("Clear"), page);
        jsPathLabel_ = new QLabel(page);
        row->addWidget(jsReloadBtn_);
        row->addWidget(jsSaveBtn_);
        row->addWidget(jsApplyBtn_);
        row->addWidget(jsResetBtn_);
        row->addWidget(jsBrowseBtn_);
        row->addWidget(jsClearBtn_);
        row->addStretch(1);
        row->addWidget(jsPathLabel_);
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
    }

    // Nanopositioner tab
    {
        auto* nanoTab = new frontend::NanopositionerTab(backend_, this);
        tabs_->addTab(nanoTab, tr("Nanopositioner"));
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
        QMessageBox::warning(this, tr("Load config.json"), tr("Failed to load: %1").arg(err));
        return;
    }
    jsonPathLabel_->setText(path);
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
        QMessageBox::warning(this, tr("Load egrabberConfig.js"), tr("Failed to load: %1").arg(err));
        return;
    }
    jsPathLabel_->setText(path);
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
    QString err;
    if (!loadFileToEditor(selected, jsonEdit_, &err)) {
        SPDLOG_WARN("Failed to load external config.json from {}: {}", selected.toStdString(), err.toStdString());
        QMessageBox::warning(this, tr("Load config.json"), tr("Failed to load: %1").arg(err));
        return;
    }
    jsonPathLabel_->setText(selected);
    if (jsonStack_ && jsonStack_->currentIndex() == 1) {
        refreshJsonTableModel();
    }
}

void ConfigTabs::onClearJson() {
    QSettings s;
    s.remove("Config/ExternalAppConfigPath");
    SPDLOG_INFO("External App config cleared; reverting to default include path");
    onReloadJson();
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
        QMessageBox::warning(this, tr("Load egrabberConfig.js"), tr("Failed to load: %1").arg(err));
        return;
    }
    jsPathLabel_->setText(selected);
}

void ConfigTabs::onClearJs() {
    QSettings s;
    s.remove("Config/ExternalCameraScriptPath");
    SPDLOG_INFO("External Camera script cleared; reverting to default include path");
    onReloadJs();
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
}

} // namespace frontend



