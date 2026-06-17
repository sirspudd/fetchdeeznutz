#include "repositorydialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QMessageBox>

namespace {
// Roles used to store remote fields structurally on each list item, so the
// remote name/URL never have to be recovered by parsing the display string
// (which breaks for names or URLs containing the " - " separator).
constexpr int RemoteNameRole = Qt::UserRole;
constexpr int RemoteUrlRole = Qt::UserRole + 1;

QListWidgetItem* makeRemoteItem(const QString& name, const QString& url)
{
    QListWidgetItem* item = new QListWidgetItem(QString("%1 - %2").arg(name, url));
    item->setData(RemoteNameRole, name);
    item->setData(RemoteUrlRole, url);
    return item;
}
} // namespace

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
        remotesList->addItem(makeRemoteItem(remote.name, remote.url));
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

    // Get remotes from the list (read structured data, not the display string)
    for (int i = 0; i < remotesList->count(); ++i) {
        QListWidgetItem* item = remotesList->item(i);
        GitRemote remote;
        remote.name = item->data(RemoteNameRole).toString();
        remote.url = item->data(RemoteUrlRole).toString();
        remote.status = "Ready";
        if (!remote.name.isEmpty()) {
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
        if (remotesList->item(i)->data(RemoteNameRole).toString() == name) {
            QMessageBox::warning(this, "Duplicate Remote", "A remote with this name already exists.");
            return;
        }
    }

    remotesList->addItem(makeRemoteItem(name, url));

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
