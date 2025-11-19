#pragma once

#include <QWidget>

#include <string>

namespace backend { class AppBackend; }

class QTabWidget;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QStackedWidget;
class QTableView;
class QToolButton;
class QTimer;

namespace frontend { class JsonTableModel; }

namespace frontend {

class ConfigTabs : public QWidget {
    Q_OBJECT
public:
    explicit ConfigTabs(backend::AppBackend& backend, QWidget* parent = nullptr);

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

private:
    QString appDirIncludePath(const QString& fileName) const;
    QString defaultJsonPath() const { return appDirIncludePath("config.json"); }
    QString defaultJsPath() const { return appDirIncludePath("egrabberConfig.js"); }
    QString currentJsonPath() const;
    QString currentJsPath() const;
    bool loadFileToEditor(const QString& path, QPlainTextEdit* editor, QString* err);
    bool saveEditorToFile(QPlainTextEdit* editor, const QString& path, QString* err);
    void refreshJsonTableModel();

    backend::AppBackend& backend_;

    QTabWidget* tabs_ = nullptr;

    // JSON tab
    QPlainTextEdit* jsonEdit_ = nullptr;
    QStackedWidget* jsonStack_ = nullptr;
    QTableView* jsonTable_ = nullptr;
    JsonTableModel* jsonModel_ = nullptr;
    QToolButton* jsonTableToggle_ = nullptr;
    QPushButton* jsonReloadBtn_ = nullptr;
    QPushButton* jsonSaveBtn_ = nullptr;
    QPushButton* jsonBrowseBtn_ = nullptr;
    QPushButton* jsonClearBtn_ = nullptr;
    QLabel* jsonPathLabel_ = nullptr;
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
};

} // namespace frontend



