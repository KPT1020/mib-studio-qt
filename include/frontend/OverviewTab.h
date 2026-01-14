#pragma once

#include <QWidget>
#include <QImage>

namespace backend { class AppBackend; }

class QPlainTextEdit;
class QPushButton;
class QToolButton;
class QLabel;
class QTimer;
class QWidget;

namespace frontend {

class OverviewTab : public QWidget {
    Q_OBJECT
public:
    explicit OverviewTab(backend::AppBackend& backend, QWidget* parent = nullptr);

    enum class FitMode { FitToWindow, Zoom100 };

private slots:
    void onTick();
    void onReloadJs();
    void onSaveJs();
    void onApplyJs();
    void onBrowseJs();
    void onClearJs();
    void onToggleFit();

private:
    QString appDirIncludePath(const QString& fileName) const;
    QString defaultJsPath() const { return appDirIncludePath("overviewConfig.js"); }
    QString currentJsPath() const;
    bool loadFileToEditor(const QString& path, QPlainTextEdit* editor, QString* err);
    bool saveEditorToFile(QPlainTextEdit* editor, const QString& path, QString* err);

    backend::AppBackend& backend_;
    
    // Frame display
    QWidget* canvas_ = nullptr;
    QTimer* timer_ = nullptr;
    QImage frameImage_;
    FitMode fitMode_ { FitMode::FitToWindow };
    QToolButton* fitBtn_ = nullptr;
    
    // Camera script configuration
    QPlainTextEdit* jsEdit_ = nullptr;
    QPushButton* jsReloadBtn_ = nullptr;
    QPushButton* jsSaveBtn_ = nullptr;
    QPushButton* jsApplyBtn_ = nullptr;
    QPushButton* jsBrowseBtn_ = nullptr;
    QPushButton* jsClearBtn_ = nullptr;
    QLabel* jsPathLabel_ = nullptr;
    QLabel* jsUnsavedLabel_ = nullptr;
};

} // namespace frontend
