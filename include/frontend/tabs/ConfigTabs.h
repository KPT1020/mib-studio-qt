#pragma once

#include <QWidget>
#include <QMap>
#include <QVector>

#include <string>
#include <optional>

#include "frontend/system/ProfileManager.h"

namespace backend { class AppBackend; }

class QTabWidget;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QStackedWidget;
class QTableView;
class QToolButton;
class QTimer;
class QComboBox;
class QCheckBox;
class QScrollArea;
class QGridLayout;
class QDialog;
class QTableWidget;

namespace frontend { class JsonTableModel; }

namespace frontend {

// Lightweight snapshot of the active Experiment Profile selection (UX-2,
// issue #306). Emitted by ConfigTabs so the workflow bar, context bar, and
// readiness gate can reflect profile state without re-scanning profiles.
struct ProfileStatus {
    QString name;                // empty when the template/defaults are active
    bool selected = false;       // an explicit profile (not the template)
    bool dirty = false;
    bool incompatible = false;
    bool updateAvailable = false;
    bool remoteManaged = false;
};

class ConfigTabs : public QWidget {
    Q_OBJECT
public:
    explicit ConfigTabs(backend::AppBackend& backend, QWidget* parent = nullptr);

signals:
	void appConfigPathChanged(const QString& path);
    void profileStatusChanged(const frontend::ProfileStatus& status);
    // UX-5 (#309): result of the Apply & Verify Profile transaction, and
    // invalidation when the profile/selection/config changes afterwards.
    void profileApplyStateChanged(bool applied, bool verified);

public:
    QString currentJsPath() const;
    frontend::ProfileStatus currentProfileStatus() const;

public slots:
    // Called when config file changes externally (e.g., when ROI is saved)
    void onExternalConfigFileChanged(const QString& path);

private slots:
    void onReloadJson();
    void onSaveJson();
    void onBrowseJson();
    void onClearJson();
    void onJsonTableToggled(bool checked);
    void onJsonTextChangedDebounced();
    void rebuildJsonFromTable();
    void onReloadJs();
    void onSaveJs();
    void onBrowseJs();
    void onClearJs();
    void onApplyJs();
    void onResetCamera();
	// Profiles
	void onProfileSelectionChanged(int index);
    void onApplyAndVerifyProfile();
	void onSaveProfile();
	void onDeleteProfile();
	void onRenameProfile();
    void onCheckProfileUpdates();
    void onUpdateSelectedProfile();
    void onShowProfileDiff();
    void onDuplicateProfileAsLocal();
    void onIncludeJsToggled(bool checked);

private:
    QString appDirIncludePath(const QString& fileName) const;
    QString defaultJsonPath() const { return appDirIncludePath("config.json"); }
    QString defaultJsPath() const { return appDirIncludePath("egrabberConfig.js"); }
    QString currentJsonPath() const;
    void clearJsonSyncIndicators();
    bool loadFileToEditor(const QString& path, QPlainTextEdit* editor, QString* err);
    bool saveEditorToFile(QPlainTextEdit* editor, const QString& path, QString* err);
    void refreshJsonTableModel();
	// Profiles helpers
	QString profilesBaseDir() const;
	bool ensureProfilesDirExists(QString* err = nullptr) const;
	QStringList listProfiles() const;
	void refreshProfilesList();
	QString sanitizeProfileName(const QString& name) const;
	bool writeTextFile(const QString& path, const QString& content, QString* err) const;
	bool readTextFile(const QString& path, QString* out, QString* err) const;
	QString profileDirPath(const QString& profileName) const;
	QString profileJsonPath(const QString& profileName) const;
	QString profileJsPath(const QString& profileName) const;
	void loadSelectedProfileInternal(const QString& profileName);
    QString selectedProfileName() const;
    QString profileLabelForSummary(const frontend::ProfileManager::LocalProfile& summary) const;
    void refreshProfileStatusLabel();
    void showDiffDialog(const QString& title, const QVector<frontend::ProfileManager::DiffRow>& rows);
    std::optional<frontend::ProfileManager::LocalProfile> selectedProfileSummary() const;
    std::optional<frontend::ProfileManager::CatalogEntry> selectedRemoteCatalogEntry() const;

    backend::AppBackend& backend_;
    frontend::ProfileManager profileManager_;
    std::optional<frontend::ProfileManager::Catalog> remoteCatalog_;

    QTabWidget* tabs_ = nullptr;

    // JSON tab
    QPlainTextEdit* jsonEdit_ = nullptr;
    QStackedWidget* jsonStack_ = nullptr;
    QTableView* jsonTable_ = nullptr;  // Legacy single table (kept for backward compatibility during transition)
    JsonTableModel* jsonModel_ = nullptr;  // Legacy model
    QScrollArea* jsonScrollArea_ = nullptr;  // Scroll area for grouped tables
    QWidget* jsonGridContainer_ = nullptr;  // Container widget with grid layout
    QGridLayout* jsonGridLayout_ = nullptr;  // Grid layout (3 columns) for grouped tables
    QMap<QString, QTableView*> jsonSectionTables_;  // Map of section name -> table widget
    QMap<QString, JsonTableModel*> jsonSectionModels_;  // Map of section name -> table model
    QToolButton* jsonTableToggle_ = nullptr;
    QPushButton* jsonReloadBtn_ = nullptr;
    QPushButton* jsonSaveBtn_ = nullptr;
    QPushButton* jsonBrowseBtn_ = nullptr;
    QPushButton* jsonClearBtn_ = nullptr;
    QLabel* jsonPathLabel_ = nullptr;
	QLabel* jsonUnsavedLabel_ = nullptr;
	QLabel* jsonConflictLabel_ = nullptr;
	QComboBox* profileSelect_ = nullptr;
	QPushButton* saveProfileBtn_ = nullptr;
	QPushButton* deleteProfileBtn_ = nullptr;
	QPushButton* renameProfileBtn_ = nullptr;
	QPushButton* checkUpdatesBtn_ = nullptr;
	QPushButton* updateSelectedBtn_ = nullptr;
	QPushButton* showDiffBtn_ = nullptr;
	QPushButton* duplicateAsLocalBtn_ = nullptr;
    QPushButton* applyAndVerifyBtn_ = nullptr;
	QLabel* profileStatusLabel_ = nullptr;
    QTimer* jsonDebounceTimer_ = nullptr;

    // JS tab
    QPlainTextEdit* jsEdit_ = nullptr;
    QPushButton* jsReloadBtn_ = nullptr;
    QPushButton* jsSaveBtn_ = nullptr;
    QPushButton* jsApplyBtn_ = nullptr;
    QPushButton* jsResetBtn_ = nullptr;
    QPushButton* jsBrowseBtn_ = nullptr;
    QPushButton* jsClearBtn_ = nullptr;
    QLabel* jsPathLabel_ = nullptr;
	QLabel* jsUnsavedLabel_ = nullptr;
	QCheckBox* profilesIncludeJsCheck_ = nullptr;
};

} // namespace frontend
