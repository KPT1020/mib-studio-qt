#pragma once

#include <QDialog>

class QLineEdit;
class QDoubleSpinBox;

namespace Ui { class MockConfigDialog; }

namespace frontend {

class MockConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit MockConfigDialog(QWidget* parent = nullptr);
    ~MockConfigDialog();

    QString folderPath() const;
    double framesPerSecond() const;

private slots:
    void onBrowseFolder();

private:
    void applyDefaultFolder();

    Ui::MockConfigDialog* ui;
};

} // namespace frontend


