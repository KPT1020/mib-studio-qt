#pragma once

#include <QDialog>

class QLineEdit;
class QDoubleSpinBox;

namespace frontend {

class MockConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit MockConfigDialog(QWidget* parent = nullptr);

    QString folderPath() const;
    double framesPerSecond() const;

private slots:
    void onBrowseFolder();

private:
    void applyDefaultFolder();

    QLineEdit* folderEdit_{nullptr};
    QDoubleSpinBox* fpsSpin_{nullptr};
};

} // namespace frontend


