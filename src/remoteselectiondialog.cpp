#include "remoteselectiondialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>

RemoteSelectionDialog::RemoteSelectionDialog(const QList<GitRemote>& remotes, QWidget *parent)
    : QDialog(parent)
    , allRemotes(remotes)
{
    setWindowTitle("Select Remotes to Monitor");
    setModal(true);
    resize(500, 300);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Instructions
    QLabel *instructionLabel = new QLabel("This repository has multiple remotes. Please select which ones you want to monitor and fetch from:");
    instructionLabel->setWordWrap(true);
    mainLayout->addWidget(instructionLabel);

    // Remote selection area
    QGroupBox *remotesGroup = new QGroupBox("Available Remotes");
    QVBoxLayout *remotesLayout = new QVBoxLayout(remotesGroup);

    // Create checkboxes for each remote
    for (const GitRemote& remote : allRemotes) {
        QCheckBox *checkbox = new QCheckBox();
        checkbox->setChecked(true); // Default to all selected
        checkbox->setText(QString("%1 - %2").arg(remote.name, remote.url));
        remoteCheckboxes.append(checkbox);
        remotesLayout->addWidget(checkbox);
    }

    mainLayout->addWidget(remotesGroup);

    // Control buttons
    QHBoxLayout *controlLayout = new QHBoxLayout();
    QPushButton *selectAllButton = new QPushButton("Select All");
    QPushButton *selectNoneButton = new QPushButton("Select None");
    
    connect(selectAllButton, &QPushButton::clicked, this, &RemoteSelectionDialog::selectAll);
    connect(selectNoneButton, &QPushButton::clicked, this, &RemoteSelectionDialog::selectNone);
    
    controlLayout->addWidget(selectAllButton);
    controlLayout->addWidget(selectNoneButton);
    controlLayout->addStretch();
    
    mainLayout->addLayout(controlLayout);

    // Dialog buttons
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(buttonBox);
}

QList<GitRemote> RemoteSelectionDialog::getSelectedRemotes() const
{
    QList<GitRemote> selectedRemotes;
    
    for (int i = 0; i < remoteCheckboxes.size() && i < allRemotes.size(); ++i) {
        if (remoteCheckboxes[i]->isChecked()) {
            selectedRemotes.append(allRemotes[i]);
        }
    }
    
    return selectedRemotes;
}

void RemoteSelectionDialog::selectAll()
{
    for (QCheckBox *checkbox : remoteCheckboxes) {
        checkbox->setChecked(true);
    }
}

void RemoteSelectionDialog::selectNone()
{
    for (QCheckBox *checkbox : remoteCheckboxes) {
        checkbox->setChecked(false);
    }
}
