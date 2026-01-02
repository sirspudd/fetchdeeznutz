#include "repositorydialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QMessageBox>

RepositoryDialog::RepositoryDialog(const GitRepository& repo, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(repo.name.isEmpty() ? "Add Repository" : "Edit Repository");
    setModal(true);
    resize(500, 500);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Basic repository info
    QGroupBox *basicGroup = new QGroupBox("Repository Information");
    QFormLayout *basicLayout = new QFormLayout(basicGroup);

    nameEdit = new QLineEdit(repo.name);
    pathEdit = new QLineEdit(repo.localPath);
    branchEdit = new QLineEdit(repo.branch.isEmpty() ? "main" : repo.branch);
    intervalSpinBox = new QSpinBox();
    intervalSpinBox->setRange(1, 1440); // 1 minute to 24 hours
    intervalSpinBox->setValue(repo.fetchInterval);
    intervalSpinBox->setSuffix(" minutes");
    enabledCheckBox = new QCheckBox();
    enabledCheckBox->setChecked(repo.enabled);

    basicLayout->addRow("Name:", nameEdit);
    basicLayout->addRow("Local Path:", pathEdit);
    basicLayout->addRow("Branch:", branchEdit);
    basicLayout->addRow("Fetch Interval:", intervalSpinBox);
    basicLayout->addRow("Enabled:", enabledCheckBox);

    mainLayout->addWidget(basicGroup);

    // Remotes section
    QGroupBox *remotesGroup = new QGroupBox("Remotes");
    QVBoxLayout *remotesLayout = new QVBoxLayout(remotesGroup);

    remotesList = new QListWidget();
    remotesList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(remotesList, &QListWidget::itemSelectionChanged, this, &RepositoryDialog::onRemoteSelectionChanged);
    remotesLayout->addWidget(remotesList);

    // Remote controls
    QHBoxLayout *remoteControlsLayout = new QHBoxLayout();
    addRemoteButton = new QPushButton("Add Remote");
    removeRemoteButton = new QPushButton("Remove Remote");
    removeRemoteButton->setEnabled(false);

    connect(addRemoteButton, &QPushButton::clicked, this, &RepositoryDialog::addRemote);
    connect(removeRemoteButton, &QPushButton::clicked, this, &RepositoryDialog::removeRemote);

    remoteControlsLayout->addWidget(addRemoteButton);
    remoteControlsLayout->addWidget(removeRemoteButton);
    remoteControlsLayout->addStretch();

    remotesLayout->addLayout(remoteControlsLayout);

    // Remote input fields
    QFormLayout *remoteInputLayout = new QFormLayout();
    remoteNameEdit = new QLineEdit();
    remoteUrlEdit = new QLineEdit();
    remoteInputLayout->addRow("Remote Name:", remoteNameEdit);
    remoteInputLayout->addRow("Remote URL:", remoteUrlEdit);

    remotesLayout->addLayout(remoteInputLayout);

    mainLayout->addWidget(remotesGroup);

    // Load existing remotes
    for (const GitRemote& remote : repo.remotes) {
        QString itemText = QString("%1 - %2").arg(remote.name, remote.url);
        remotesList->addItem(itemText);
    }

    // Dialog buttons
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(buttonBox);
}

GitRepository RepositoryDialog::getRepository() const
{
    GitRepository repo;
    repo.name = nameEdit->text().trimmed();
    repo.localPath = pathEdit->text().trimmed();
    repo.branch = branchEdit->text().trimmed();
    repo.fetchInterval = intervalSpinBox->value();
    repo.enabled = enabledCheckBox->isChecked();

    // Get remotes from the list
    for (int i = 0; i < remotesList->count(); ++i) {
        QString itemText = remotesList->item(i)->text();
        QStringList parts = itemText.split(" - ");
        if (parts.size() >= 2) {
            GitRemote remote;
            remote.name = parts[0];
            remote.url = parts[1];
            remote.status = "Ready";
            repo.remotes.append(remote);
        }
    }

    return repo;
}

void RepositoryDialog::addRemote()
{
    QString name = remoteNameEdit->text().trimmed();
    QString url = remoteUrlEdit->text().trimmed();

    if (name.isEmpty() || url.isEmpty()) {
        QMessageBox::warning(this, "Invalid Remote", "Remote name and URL are required.");
        return;
    }

    // Check if remote name already exists
    for (int i = 0; i < remotesList->count(); ++i) {
        QString itemText = remotesList->item(i)->text();
        if (itemText.startsWith(name + " - ")) {
            QMessageBox::warning(this, "Duplicate Remote", "A remote with this name already exists.");
            return;
        }
    }

    QString itemText = QString("%1 - %2").arg(name, url);
    remotesList->addItem(itemText);

    remoteNameEdit->clear();
    remoteUrlEdit->clear();
}

void RepositoryDialog::removeRemote()
{
    int currentRow = remotesList->currentRow();
    if (currentRow >= 0) {
        delete remotesList->takeItem(currentRow);
    }
}

void RepositoryDialog::onRemoteSelectionChanged()
{
    bool hasSelection = remotesList->currentRow() >= 0;
    removeRemoteButton->setEnabled(hasSelection);
}
