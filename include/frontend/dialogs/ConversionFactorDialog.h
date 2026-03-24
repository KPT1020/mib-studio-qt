#pragma once

#include <QDialog>

class QDoubleSpinBox;
class QDialogButtonBox;

namespace backend { class AppBackend; }
namespace Ui { class ConversionFactorDialog; }

class ConversionFactorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConversionFactorDialog(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~ConversionFactorDialog();

private slots:
    void onApply();

private:
    void applySettings();
    void saveConversionFactorToConfig(double factor);

    Ui::ConversionFactorDialog* ui;
    backend::AppBackend& backend_;
};
