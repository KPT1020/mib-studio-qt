#include "frontend/MockConfigDialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QDir>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWidget>

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
        : QDialog(parent)
    {
        setWindowTitle(tr("Mock Camera Settings"));
        setModal(true);

        folderEdit_ = new QLineEdit(this);
        folderEdit_->setPlaceholderText(tr("Select a folder containing image frames"));
        applyDefaultFolder();

        auto *browseButton = new QPushButton(tr("Browse..."), this);
        connect(browseButton, &QPushButton::clicked, this, &MockConfigDialog::onBrowseFolder);

        auto *folderLayout = new QHBoxLayout();
        folderLayout->setContentsMargins(0, 0, 0, 0);
        folderLayout->addWidget(folderEdit_);
        folderLayout->addWidget(browseButton);
        auto *folderWidget = new QWidget(this);
        folderWidget->setLayout(folderLayout);

        fpsSpin_ = new QDoubleSpinBox(this);
        fpsSpin_->setRange(1, 10000.0);
        // fpsSpin_->setDecimals(0);
        fpsSpin_->setSuffix(tr(" fps"));
        fpsSpin_->setValue(5000.0);

        auto *formLayout = new QFormLayout();
        formLayout->addRow(tr("Frame folder"), folderWidget);
        formLayout->addRow(tr("Frame rate"), fpsSpin_);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto *rootLayout = new QVBoxLayout(this);
        rootLayout->addLayout(formLayout);
        rootLayout->addWidget(buttons);
        setLayout(rootLayout);
    }

    QString MockConfigDialog::folderPath() const
    {
        return folderEdit_ ? folderEdit_->text().trimmed() : QString();
    }

    double MockConfigDialog::framesPerSecond() const
    {
        return fpsSpin_ ? fpsSpin_->value() : 30.0;
    }

    void MockConfigDialog::onBrowseFolder()
    {
        const QString current = folderPath();
        QString selected = QFileDialog::getExistingDirectory(this,
                                                             tr("Select mock image folder"),
                                                             current.isEmpty() ? defaultMockFolder() : current);
        if (!selected.isEmpty())
        {
            folderEdit_->setText(QDir(selected).absolutePath());
        }
    }

    void MockConfigDialog::applyDefaultFolder()
    {
        if (!folderEdit_)
            return;
        const QString initial = defaultMockFolder();
        folderEdit_->setText(QDir(initial).absolutePath());
    }

} // namespace frontend
