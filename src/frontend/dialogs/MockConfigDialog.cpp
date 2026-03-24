#include "frontend/dialogs/MockConfigDialog.h"
#include "ui_MockConfigDialog.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QDir>
#include <QStandardPaths>

#include <filesystem>

namespace
{

    QString defaultMockFolder()
    {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString fallback = QDir(appDir).absoluteFilePath("../data/mock_frames");
        if (QDir(fallback).exists())
        {
            return QDir(fallback).absolutePath();
        }
        const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        return documents.isEmpty() ? QDir::currentPath() : documents;
    }

} // namespace

namespace frontend
{

    MockConfigDialog::MockConfigDialog(QWidget *parent)
        : QDialog(parent), ui(new Ui::MockConfigDialog)
    {
        ui->setupUi(this);
        applyDefaultFolder();
        connect(ui->browseButton, &QPushButton::clicked, this, &MockConfigDialog::onBrowseFolder);
        connect(ui->buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(ui->buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    MockConfigDialog::~MockConfigDialog() {
        delete ui;
    }

    QString MockConfigDialog::folderPath() const
    {
        return ui->folderEdit->text().trimmed();
    }

    double MockConfigDialog::framesPerSecond() const
    {
        return ui->fpsSpin->value();
    }

    void MockConfigDialog::onBrowseFolder()
    {
        const QString current = folderPath();
        QString selected = QFileDialog::getExistingDirectory(this,
                                                             tr("Select mock image folder"),
                                                             current.isEmpty() ? defaultMockFolder() : current);
        if (!selected.isEmpty())
        {
            ui->folderEdit->setText(QDir(selected).absolutePath());
        }
    }

    void MockConfigDialog::applyDefaultFolder()
    {
        const QString initial = defaultMockFolder();
        ui->folderEdit->setText(QDir(initial).absolutePath());
    }

} // namespace frontend
