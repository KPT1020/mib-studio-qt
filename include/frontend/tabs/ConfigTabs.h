#pragma once

#include <QWidget>
#include <QMap>
#include <QVector>

#include <atomic>
#include <optional>
#include <string>
#include <thread>

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
class QSpinBox;
class QDoubleSpinBox;

namespace frontend { class JsonTableModel; }

namespace frontend {

class ConfigTabs : public QWidget {
    Q_OBJECT
public:
    explicit ConfigTabs(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~ConfigTabs() override;

signals:
	void appConfigPathChanged(const QString& path);

public:
    QString currentJsPath() const;

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
    // MindVision config tab
    void onReloadMv();
    void onSaveMv();
    void onBrowseMv();
    void onClearMv();
    void onApplyMvConfig();
    void onSoftTrigger();
    void onMvFormChanged();
    void onMvTextChangedDebounced();
    void onPulseGenConnectToggle();
    void onPulseGenApplySettings();
    void onPulseGenStart();
    void onPulseGenStop();
    void onPulseGenRefreshPorts();
    void onPulseGenScanToggle();
	// Profiles
	void onProfileSelectionChanged(int index);
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
    QString defaultMvJsonPath() const { return appDirIncludePath("mindvisionConfig.json"); }
    QString currentJsonPath() const;
    QString currentMvJsonPath() const;
    void refreshPulseGenUi();
    void refreshPulseGenPorts();
    void savePulseGenSettings() const;
    void restorePulseGenSettings();
    void stopPulseGenScan();
    void syncMvFormFromJson();
    void syncMvJsonFromForm();
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

    // MindVision config tab (acquisition trigger + strobe; NOT the sort pulse)
    QPlainTextEdit* mvEdit_ = nullptr;
    QPushButton* mvReloadBtn_ = nullptr;
    QPushButton* mvSaveBtn_ = nullptr;
    QPushButton* mvApplyBtn_ = nullptr;
    QPushButton* mvSoftTriggerBtn_ = nullptr;
    QPushButton* mvBrowseBtn_ = nullptr;
    QPushButton* mvClearBtn_ = nullptr;
    QLabel* mvPathLabel_ = nullptr;
    QLabel* mvUnsavedLabel_ = nullptr;
    // Trigger & strobe parameter form (two-way synced with the JSON editor)
    QComboBox* mvTriggerModeCombo_ = nullptr;
    QComboBox* mvSignalTypeCombo_ = nullptr;
    QDoubleSpinBox* mvExposureSpin_ = nullptr;
    QSpinBox* mvTrigDelaySpin_ = nullptr;
    QSpinBox* mvJitterSpin_ = nullptr;
    QSpinBox* mvTrigCountSpin_ = nullptr;
    QComboBox* mvStrobeModeCombo_ = nullptr;
    QSpinBox* mvStrobeDelaySpin_ = nullptr;
    QSpinBox* mvStrobeWidthSpin_ = nullptr;
    QComboBox* mvStrobePolarityCombo_ = nullptr;
    QTimer* mvDebounceTimer_ = nullptr;
    bool mvSyncGuard_ = false;
    // Pulse generator (Zhongsheng module = external trigger source).
    // Device identity is (port, bus settings, Modbus slave address); the
    // channel is a setting below that identity.
    QComboBox* pgPortCombo_ = nullptr;
    QPushButton* pgRefreshPortsBtn_ = nullptr;
    QComboBox* pgBaudCombo_ = nullptr;
    QComboBox* pgDataBitsCombo_ = nullptr;
    QComboBox* pgParityCombo_ = nullptr;
    QComboBox* pgStopBitsCombo_ = nullptr;
    QSpinBox* pgAddrSpin_ = nullptr;
    QPushButton* pgScanBtn_ = nullptr;
    QPushButton* pgConnectBtn_ = nullptr;
    std::thread pgScanThread_;
    std::atomic<bool> pgScanCancel_{false};
    bool pgScanRunning_ = false;
    QSpinBox* pgChannelSpin_ = nullptr;
    QDoubleSpinBox* pgFreqSpin_ = nullptr;
    QDoubleSpinBox* pgDutySpin_ = nullptr;
    QPushButton* pgApplyBtn_ = nullptr;
    QPushButton* pgStartBtn_ = nullptr;
    QPushButton* pgStopBtn_ = nullptr;
    QLabel* pgStatusLabel_ = nullptr;
};

} // namespace frontend
