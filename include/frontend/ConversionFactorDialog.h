#pragma once

#include <QDialog>

class QDoubleSpinBox;
class QDialogButtonBox;

namespace backend { class AppBackend; }

class ConversionFactorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConversionFactorDialog(backend::AppBackend& backend, QWidget* parent = nullptr);

private slots:
    void onApply();

private:
    void applySettings();
    void saveConversionFactorToConfig(double factor);

    backend::AppBackend& backend_;
    QDoubleSpinBox* conversionFactorSpin_{nullptr};
    QDialogButtonBox* buttons_{nullptr};
};
