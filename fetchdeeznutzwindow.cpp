#include "fetchdeeznutzwindow.h"

// Custom error code for connection timeout
#define GIT_ETIMEOUT -1000

GitFetchWorker::GitFetchWorker(QObject *parent)
    : QObject(parent)
    , m_stopRequested(false)
    , m_timeoutSeconds(300) // Default 5 minutes
    , m_connectionTimeoutSeconds(5) // Default 5 seconds
{
}

GitFetchWorker::~GitFetchWorker()
{
}

void GitFetchWorker::fetchRepository(const GitRepository& repo)
{
    m_stopRequested = false;
    emit fetchStarted(repo.name);
    performFetch(repo);
}

void GitFetchWorker::stopFetching()
{
    m_stopRequested = true;
}

void GitFetchWorker::setTimeout(int timeoutSeconds)
{
    m_timeoutSeconds = timeoutSeconds;
}

void GitFetchWorker::setConnectionTimeout(int timeoutSeconds)
{
    m_connectionTimeoutSeconds = timeoutSeconds;
}

void GitFetchWorker::performFetch(const GitRepository& repo)
{
    if (m_stopRequested) {
        emit fetchFinished(repo.name, false, "Fetch cancelled");
        return;
    }

    if (repo.remotes.isEmpty()) {
        emit fetchError(repo.name, "No remotes configured");
        return;
    }

    // Check if repository exists
    if (!isRepositoryValid(repo.localPath)) {
        emit fetchError(repo.name, QString("Repository not found at: %1").arg(repo.localPath));
        return;
    }

    // Open the repository
    git_repository *repository = nullptr;
    int error = git_repository_open(&repository, repo.localPath.toLocal8Bit().constData());
    if (error < 0) {
        emit fetchError(repo.name, getGitErrorMessage(error));
        return;
    }

    // Set up timeout timer
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setInterval(m_timeoutSeconds * 1000); // Convert to milliseconds
    
    bool timeoutOccurred = false;
    connect(&timeoutTimer, &QTimer::timeout, [&timeoutOccurred, &repo, this]() {
        timeoutOccurred = true;
        m_stopRequested = true;
        emit fetchError(repo.name, QString("Fetch timed out after %1 seconds").arg(m_timeoutSeconds));
    });
    
    timeoutTimer.start();

    bool allSuccessful = true;
    QStringList failedRemotes;
    int totalRemotes = repo.remotes.size();
    int completedRemotes = 0;

    // Fetch from all remotes
    for (const GitRemote& remote : repo.remotes) {
        if (m_stopRequested || timeoutOccurred) {
            git_repository_free(repository);
            if (timeoutOccurred) {
                emit fetchFinished(repo.name, false, QString("Fetch timed out after %1 seconds").arg(m_timeoutSeconds));
            } else {
                emit fetchFinished(repo.name, false, "Fetch cancelled");
            }
            return;
        }

        emit fetchProgress(repo.name, remote.name, (completedRemotes * 100) / totalRemotes);

        git_remote *git_remote = nullptr;
        error = git_remote_lookup(&git_remote, repository, remote.name.toLocal8Bit().constData());
        if (error < 0) {
            // If remote doesn't exist, add it
            error = git_remote_create(&git_remote, repository, remote.name.toLocal8Bit().constData(), remote.url.toLocal8Bit().constData());
            if (error < 0) {
                failedRemotes.append(remote.name);
                allSuccessful = false;
                completedRemotes++;
                continue;
            }
        }

        // Fetch from remote
        git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;

        // Set up authentication callback for SSH
        fetch_opts.callbacks.credentials = [](git_credential **out, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload) -> int {
            GitFetchWorker *worker = static_cast<GitFetchWorker*>(payload);
            return worker->sshKeyCallback(out, url, username_from_url, allowed_types, payload);
        };
        fetch_opts.callbacks.payload = this;

        // Don't automatically download tags - let user control this
        fetch_opts.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_NONE;
        
        // Try to fetch with connection timeout
        error = fetchRemoteWithTimeout(git_remote, fetch_opts, m_connectionTimeoutSeconds);

        if (error < 0) {
            if (error == GIT_ETIMEOUT) {
                // Connection timeout - this is a custom error code we return
                failedRemotes.append(remote.name + " (connection timeout)");
            } else {
                failedRemotes.append(remote.name);
            }
            allSuccessful = false;
        }

        git_remote_free(git_remote);
        completedRemotes++;
    }

    git_repository_free(repository);
    timeoutTimer.stop();

    if (timeoutOccurred) {
        emit fetchFinished(repo.name, false, QString("Fetch timed out after %1 seconds").arg(m_timeoutSeconds));
    } else if (allSuccessful) {
        emit fetchFinished(repo.name, true, "All remotes fetched successfully");
    } else {
        emit fetchFinished(repo.name, false, QString("Some remotes failed: %1").arg(failedRemotes.join(", ")));
    }
}

QString GitFetchWorker::getGitErrorMessage(int error) const
{
    if (error == GIT_ETIMEOUT) {
        return QString("Connection timeout after %1 seconds").arg(m_connectionTimeoutSeconds);
    }
    
    const git_error *git_error = git_error_last();
    if (git_error && git_error->message) {
        return QString::fromUtf8(git_error->message);
    }
    return QString("Unknown Git error: %1").arg(error);
}

int GitFetchWorker::sshKeyCallback(git_credential **out, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload)
{
    // Try SSH key authentication first
    if (allowed_types & GIT_CREDENTIAL_SSH_KEY) {
        // Use SSH agent if available
        int error = git_credential_ssh_key_from_agent(out, username_from_url);
        if (error == 0) {
            return 0;
        }

        // Fall back to default SSH key locations
        QString homeDir = QDir::homePath();
        QStringList sshKeyPaths = {
            homeDir + "/.ssh/id_rsa",
            homeDir + "/.ssh/id_ed25519",
            homeDir + "/.ssh/id_ecdsa",
            homeDir + "/.ssh/id_dsa"
        };

        for (const QString& keyPath : sshKeyPaths) {
            if (QFile::exists(keyPath)) {
                error = git_credential_ssh_key_new(out, username_from_url, nullptr, keyPath.toLocal8Bit().constData(), nullptr);
                if (error == 0) {
                    return 0;
                }
            }
        }
    }

    return GIT_EUSER; // User cancelled
}

void GitFetchWorker::calculateRemoteCommitCounts(git_repository* repository, GitRemote& remote, const QString& branch)
{
    // This is a simplified version - the full implementation would be similar to the original
    // For now, just set default values
    remote.commitsAhead = 0;
    remote.commitsBehind = 0;
}

bool GitFetchWorker::isRepositoryValid(const QString& path)
{
    git_repository *repository = nullptr;
    int error = git_repository_open(&repository, path.toLocal8Bit().constData());

    if (error < 0) {
        return false;
    }

    git_repository_free(repository);
    return true;
}

int GitFetchWorker::fetchRemoteWithTimeout(git_remote* git_remote, const git_fetch_options& fetch_opts, int timeoutSeconds)
{
    // Create a future that runs the git fetch operation
    QFuture<int> future = QtConcurrent::run([git_remote, fetch_opts]() -> int {
        return git_remote_fetch(git_remote, nullptr, &fetch_opts, nullptr);
    });
    
    // Use QFutureWatcher to monitor the future with timeout
    QFutureWatcher<int> watcher;
    watcher.setFuture(future);
    
    // Create a timer for timeout
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setInterval(timeoutSeconds * 1000);
    
    // Use event loop to wait for completion or timeout
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<int>::finished, &loop, &QEventLoop::quit);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    
    timeoutTimer.start();
    loop.exec();
    
    if (timeoutTimer.isActive()) {
        // Operation completed before timeout
        timeoutTimer.stop();
        return future.result();
    } else {
        // Timeout occurred - we can't actually cancel the git operation,
        // but we can return a timeout error code
        return GIT_ETIMEOUT;
    }
}

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

FetchDeeznutzWindow::FetchDeeznutzWindow(QWidget *parent)
    : QMainWindow(parent)
    , fetchTimer(new QTimer(this))
    , fetchThread(new QThread(this))
    , fetchWorker(new GitFetchWorker())
    , currentFetchIndex(-1)
    , isFetching(false)
{
    setWindowTitle("Git Repository Fetcher");
    setMinimumSize(800, 600);

    // Setup background thread and worker
    fetchWorker->moveToThread(fetchThread);
    connect(fetchThread, &QThread::finished, fetchWorker, &GitFetchWorker::deleteLater);
    connect(fetchWorker, &GitFetchWorker::fetchStarted, this, &FetchDeeznutzWindow::onBackgroundFetchStarted);
    connect(fetchWorker, &GitFetchWorker::fetchProgress, this, &FetchDeeznutzWindow::onBackgroundFetchProgress);
    connect(fetchWorker, &GitFetchWorker::fetchFinished, this, &FetchDeeznutzWindow::onBackgroundFetchFinished);
    connect(fetchWorker, &GitFetchWorker::fetchError, this, &FetchDeeznutzWindow::onBackgroundFetchError);
    fetchThread->start();
    
    // Set initial timeout values
    QMetaObject::invokeMethod(fetchWorker, "setTimeout", Qt::QueuedConnection, Q_ARG(int, 300)); // 5 minutes default
    QMetaObject::invokeMethod(fetchWorker, "setConnectionTimeout", Qt::QueuedConnection, Q_ARG(int, 5)); // 5 seconds default

    setupUI();
    loadRepositories();
    updateRepositoryTree();

    // Connect timer for scheduled fetching
    connect(fetchTimer, &QTimer::timeout, this, &FetchDeeznutzWindow::performScheduledFetch);

    // Initialize libgit2
    git_libgit2_init();

    // Start with a 1-minute timer
    fetchTimer->start(60000); // 60 seconds
}

FetchDeeznutzWindow::~FetchDeeznutzWindow()
{
    saveRepositories();
    
    // Clean up background thread
    if (fetchWorker) {
        fetchWorker->stopFetching();
    }
    fetchThread->quit();
    fetchThread->wait();
    
    git_libgit2_shutdown();
}

void FetchDeeznutzWindow::setupUI()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);

    // Left panel - Repository list and controls
    QVBoxLayout *leftLayout = new QVBoxLayout();

    // Repository list
    QGroupBox *repoGroup = new QGroupBox("Repositories");
    QVBoxLayout *repoLayout = new QVBoxLayout(repoGroup);

    repositoryTree = new QTreeWidget();
    repositoryTree->setSelectionMode(QAbstractItemView::SingleSelection);
    repositoryTree->setHeaderLabel("Repositories");
    repositoryTree->setRootIsDecorated(true);
    repositoryTree->setAlternatingRowColors(true);
    // Tooltips are set directly on tree items
    connect(repositoryTree, &QTreeWidget::itemSelectionChanged, this, &FetchDeeznutzWindow::onRepositorySelectionChanged);
    repoLayout->addWidget(repositoryTree);

    // Repository control buttons
    QHBoxLayout *repoButtonLayout = new QHBoxLayout();
    addButton = new QPushButton("Add Repo");
    addDirectoryButton = new QPushButton("Add Directory");
    editButton = new QPushButton("Edit");
    removeButton = new QPushButton("Remove");
    fetchSelectedButton = new QPushButton("Fetch Selected");

    connect(addButton, &QPushButton::clicked, this, &FetchDeeznutzWindow::addRepository);
    connect(addDirectoryButton, &QPushButton::clicked, this, &FetchDeeznutzWindow::addDirectory);
    connect(editButton, &QPushButton::clicked, this, &FetchDeeznutzWindow::editRepository);
    connect(removeButton, &QPushButton::clicked, this, &FetchDeeznutzWindow::removeRepository);
    connect(fetchSelectedButton, &QPushButton::clicked, this, &FetchDeeznutzWindow::fetchSelected);

    repoButtonLayout->addWidget(addButton);
    repoButtonLayout->addWidget(addDirectoryButton);
    repoButtonLayout->addWidget(editButton);
    repoButtonLayout->addWidget(removeButton);
    repoButtonLayout->addWidget(fetchSelectedButton);

    repoLayout->addLayout(repoButtonLayout);
    leftLayout->addWidget(repoGroup);

    // Global controls
    settingsGroup = new QGroupBox("Settings");
    QFormLayout *settingsLayout = new QFormLayout(settingsGroup);

    globalIntervalSpinBox = new QSpinBox();
    globalIntervalSpinBox->setRange(1, 1440);
    globalIntervalSpinBox->setValue(60);
    globalIntervalSpinBox->setSuffix(" minutes");
    connect(globalIntervalSpinBox, &QSpinBox::valueChanged, this, &FetchDeeznutzWindow::onFetchIntervalChanged);

    fetchTimeoutSpinBox = new QSpinBox();
    fetchTimeoutSpinBox->setRange(10, 3600); // 10 seconds to 1 hour
    fetchTimeoutSpinBox->setValue(300); // Default 5 minutes
    fetchTimeoutSpinBox->setSuffix(" seconds");
    connect(fetchTimeoutSpinBox, &QSpinBox::valueChanged, this, &FetchDeeznutzWindow::onFetchTimeoutChanged);

    connectionTimeoutSpinBox = new QSpinBox();
    connectionTimeoutSpinBox->setRange(1, 60); // 1 second to 1 minute
    connectionTimeoutSpinBox->setValue(5); // Default 5 seconds
    connectionTimeoutSpinBox->setSuffix(" seconds");
    connect(connectionTimeoutSpinBox, &QSpinBox::valueChanged, this, &FetchDeeznutzWindow::onConnectionTimeoutChanged);

    autoFetchCheckBox = new QCheckBox("Enable Auto Fetch");
    autoFetchCheckBox->setChecked(true);
    connect(autoFetchCheckBox, &QCheckBox::toggled, this, &FetchDeeznutzWindow::onAutoFetchToggled);

    fetchAllButton = new QPushButton("Fetch All Now");
    connect(fetchAllButton, &QPushButton::clicked, this, &FetchDeeznutzWindow::fetchAll);

    settingsLayout->addRow("Global Interval:", globalIntervalSpinBox);
    settingsLayout->addRow("Fetch Timeout:", fetchTimeoutSpinBox);
    settingsLayout->addRow("Connection Timeout:", connectionTimeoutSpinBox);
    settingsLayout->addRow("", autoFetchCheckBox);
    settingsLayout->addRow("", fetchAllButton);

    leftLayout->addWidget(settingsGroup);

    // Fetch Status section
    fetchStatusGroup = new QGroupBox("Active Fetches");
    fetchStatusLayout = new QVBoxLayout(fetchStatusGroup);
    fetchStatusGroup->setVisible(false); // Initially hidden
    leftLayout->addWidget(fetchStatusGroup);

    leftLayout->addStretch();

    // Right panel - Log
    QGroupBox *logGroup = new QGroupBox("Activity Log");
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);

    logTextEdit = new QTextEdit();
    logTextEdit->setReadOnly(true);
    logTextEdit->document()->setMaximumBlockCount(1000); // Limit log size
    logLayout->addWidget(logTextEdit);

    mainLayout->addLayout(leftLayout, 1);
    mainLayout->addWidget(logGroup, 1);

    // Update button states
    onRepositorySelectionChanged();
}

void FetchDeeznutzWindow::addRepository()
{
    RepositoryDialog dialog(GitRepository(), this);
    if (dialog.exec() == QDialog::Accepted) {
        GitRepository repo = dialog.getRepository();
        if (!repo.name.isEmpty() && !repo.remotes.isEmpty()) {
            repositories.append(repo);
            updateRepositoryTree();
            saveRepositories();
            logMessage(QString("Added repository: %1 with %2 remotes").arg(repo.name).arg(repo.remotes.size()));
        } else {
            QMessageBox::warning(this, "Invalid Repository", "Name and at least one remote are required.");
        }
    }
}

void FetchDeeznutzWindow::addDirectory()
{
    QString directoryPath = QFileDialog::getExistingDirectory(
        this,
        "Select Directory to Scan for Git Repositories",
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (!directoryPath.isEmpty()) {
        logMessage(QString("Scanning directory: %1").arg(directoryPath));
        scanDirectoryForRepositories(directoryPath);
    }
}

void FetchDeeznutzWindow::editRepository()
{
    QTreeWidgetItem* currentItem = repositoryTree->currentItem();
    GitRepository* repo = getRepositoryFromTreeItem(currentItem);
    if (repo) {
        RepositoryDialog dialog(*repo, this);
        if (dialog.exec() == QDialog::Accepted) {
            GitRepository newRepo = dialog.getRepository();
            if (!newRepo.name.isEmpty() && !newRepo.remotes.isEmpty()) {
                *repo = newRepo;
                updateRepositoryTree();
                saveRepositories();
                logMessage(QString("Updated repository: %1 with %2 remotes").arg(repo->name).arg(repo->remotes.size()));
            } else {
                QMessageBox::warning(this, "Invalid Repository", "Name and at least one remote are required.");
            }
        }
    }
}

void FetchDeeznutzWindow::removeRepository()
{
    QTreeWidgetItem* currentItem = repositoryTree->currentItem();
    GitRepository* repo = getRepositoryFromTreeItem(currentItem);
    if (repo) {
        QString repoName = repo->name;
        int ret = QMessageBox::question(this, "Remove Repository",
                                       QString("Are you sure you want to remove '%1'?").arg(repoName),
                                       QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            repositories.removeOne(*repo);
            updateRepositoryTree();
            saveRepositories();
            logMessage(QString("Removed repository: %1").arg(repoName));
        }
    }
}

void FetchDeeznutzWindow::fetchSelected()
{
    QTreeWidgetItem* currentItem = repositoryTree->currentItem();
    GitRepository* repo = getRepositoryFromTreeItem(currentItem);
    if (repo) {
        // Use background worker for fetching
        QMetaObject::invokeMethod(fetchWorker, "fetchRepository", Qt::QueuedConnection, Q_ARG(GitRepository, *repo));
    }
}

void FetchDeeznutzWindow::fetchAll()
{
    logMessage("Starting fetch for all enabled repositories...");
    for (GitRepository& repo : repositories) {
        if (repo.enabled) {
            // Use background worker for fetching
            QMetaObject::invokeMethod(fetchWorker, "fetchRepository", Qt::QueuedConnection, Q_ARG(GitRepository, repo));
        }
    }
}

void FetchDeeznutzWindow::onRepositorySelectionChanged()
{
    QTreeWidgetItem* currentItem = repositoryTree->currentItem();
    bool hasSelection = currentItem && getRepositoryFromTreeItem(currentItem) != nullptr;
    editButton->setEnabled(hasSelection);
    removeButton->setEnabled(hasSelection);
    fetchSelectedButton->setEnabled(hasSelection);
}

void FetchDeeznutzWindow::onFetchIntervalChanged()
{
    if (autoFetchCheckBox->isChecked()) {
        fetchTimer->setInterval(globalIntervalSpinBox->value() * 60000);
        logMessage(QString("Auto-fetch interval changed to %1 minutes").arg(globalIntervalSpinBox->value()));
    }
}

void FetchDeeznutzWindow::onFetchTimeoutChanged()
{
    int timeoutSeconds = fetchTimeoutSpinBox->value();
    QMetaObject::invokeMethod(fetchWorker, "setTimeout", Qt::QueuedConnection, Q_ARG(int, timeoutSeconds));
    logMessage(QString("Fetch timeout changed to %1 seconds").arg(timeoutSeconds));
}

void FetchDeeznutzWindow::onConnectionTimeoutChanged()
{
    int timeoutSeconds = connectionTimeoutSpinBox->value();
    QMetaObject::invokeMethod(fetchWorker, "setConnectionTimeout", Qt::QueuedConnection, Q_ARG(int, timeoutSeconds));
    logMessage(QString("Connection timeout changed to %1 seconds").arg(timeoutSeconds));
}

void FetchDeeznutzWindow::onAutoFetchToggled()
{
    if (autoFetchCheckBox->isChecked()) {
        startScheduledFetch();
        logMessage("Auto-fetch enabled");
    } else {
        stopScheduledFetch();
        logMessage("Auto-fetch disabled");
    }
}

void FetchDeeznutzWindow::performScheduledFetch()
{
    if (!autoFetchCheckBox->isChecked()) return;

    QDateTime now = QDateTime::currentDateTime();
    for (GitRepository& repo : repositories) {
        if (!repo.enabled) continue;

        QDateTime lastFetch = QDateTime::fromString(repo.lastFetch, Qt::ISODate);
        if (!lastFetch.isValid() || lastFetch.addSecs(repo.fetchInterval * 60) <= now) {
            // Use background worker for scheduled fetching
            QMetaObject::invokeMethod(fetchWorker, "fetchRepository", Qt::QueuedConnection, Q_ARG(GitRepository, repo));
        }
    }
}

void FetchDeeznutzWindow::onFetchFinished()
{
    if (currentFetchIndex >= 0 && currentFetchIndex < repositories.size()) {
        GitRepository& repo = repositories[currentFetchIndex];
        repo.status = "Success";
        repo.lastFetch = QDateTime::currentDateTime().toString(Qt::ISODate);
        logMessage(QString("✓ Successfully fetched: %1").arg(repo.name));

        updateRepositoryTree();
        saveRepositories();
        currentFetchIndex = -1;
    }

    isFetching = false;
}

void FetchDeeznutzWindow::onFetchError(const QString& errorMessage)
{
    if (currentFetchIndex >= 0 && currentFetchIndex < repositories.size()) {
        GitRepository& repo = repositories[currentFetchIndex];
        repo.status = "Error";
        logMessage(QString("✗ Error fetching: %1 - %2").arg(repo.name, errorMessage));
        updateRepositoryTree();
        saveRepositories();
        currentFetchIndex = -1;
    }

    isFetching = false;
}

void FetchDeeznutzWindow::updateRepositoryTree()
{
    repositoryTree->clear();

    // Create a map to organize repositories by path
    QMap<QString, QList<GitRepository*>> pathMap;
    for (GitRepository& repo : repositories) {
        QString dirPath = QFileInfo(repo.localPath).absolutePath();
        pathMap[dirPath].append(&repo);
    }

    // Create tree structure
    for (auto it = pathMap.begin(); it != pathMap.end(); ++it) {
        QString path = it.key();
        QList<GitRepository*> repos = it.value();

        // Create path item
        QTreeWidgetItem* pathItem = findOrCreatePathItem(path);

        // Add repositories under this path
        for (GitRepository* repo : repos) {
            QString statusIcon = repo->enabled ? "●" : "○";
            QString statusText = repo->status.isEmpty() ? "Ready" : repo->status;
            
            // Add special icons for different status types
            if (statusText == "Timeout") {
                statusIcon = "⏰";
            } else if (statusText == "Error") {
                statusIcon = "❌";
            } else if (statusText == "Success") {
                statusIcon = "✅";
            } else if (statusText == "Fetching...") {
                statusIcon = "🔄";
            }
            
            QString remotesText = QString("%1 remotes").arg(repo->remotes.size());

            // Calculate total commit deltas
            int totalAhead = 0, totalBehind = 0;
            for (const GitRemote& remote : repo->remotes) {
                totalAhead += remote.commitsAhead;
                totalBehind += remote.commitsBehind;
            }

            QString commitDeltaText;
            if (totalAhead > 0 && totalBehind > 0) {
                commitDeltaText = QString(" [+%1/-%2]").arg(totalAhead).arg(totalBehind);
            } else if (totalAhead > 0) {
                commitDeltaText = QString(" [+%1]").arg(totalAhead);
            } else if (totalBehind > 0) {
                commitDeltaText = QString(" [-%1]").arg(totalBehind);
            } else {
                commitDeltaText = " [up-to-date]";
            }

            QString itemText = QString("%1 %2 - %3 (%4) [%5]%6")
                              .arg(statusIcon)
                              .arg(repo->name)
                              .arg(statusText)
                              .arg(repo->branch)
                              .arg(remotesText)
                              .arg(commitDeltaText);

            QTreeWidgetItem* repoItem = new QTreeWidgetItem(pathItem);
            repoItem->setText(0, itemText);
            repoItem->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void*>(repo)));
            
            // Set tooltip for the repository item
            QString tooltip = generateRepositoryTooltip(*repo);
            repoItem->setToolTip(0, tooltip);
        }
    }

    // Expand all items by default
    repositoryTree->expandAll();
}

void FetchDeeznutzWindow::startScheduledFetch()
{
    fetchTimer->setInterval(globalIntervalSpinBox->value() * 60000);
    fetchTimer->start();
}

void FetchDeeznutzWindow::stopScheduledFetch()
{
    fetchTimer->stop();
}

void FetchDeeznutzWindow::fetchRepository(GitRepository& repo)
{
    if (isFetching) {
        logMessage("Another fetch operation is already in progress");
        return;
    }

    if (repo.remotes.isEmpty()) {
        logMessage(QString("No remotes configured for repository: %1").arg(repo.name));
        return;
    }

    logMessage(QString("Starting fetch for: %1 (%2 remotes)").arg(repo.name).arg(repo.remotes.size()));
    repo.status = "Fetching...";
    updateRepositoryTree();

    currentFetchIndex = repositories.indexOf(repo);
    isFetching = true;

    // Check if repository exists
    if (!isRepositoryValid(repo.localPath)) {
        onFetchError(QString("Repository not found at: %1").arg(repo.localPath));
        return;
    }

    // Open the repository
    git_repository *repository = nullptr;
    int error = git_repository_open(&repository, repo.localPath.toLocal8Bit().constData());
    if (error < 0) {
        onFetchError(getGitErrorMessage(error));
        return;
    }

    bool allSuccessful = true;
    QStringList failedRemotes;

    // Fetch from all remotes
    for (GitRemote& remote : repo.remotes) {
        logMessage(QString("Fetching from remote: %1 (%2)").arg(remote.name, remote.url));
        remote.status = "Fetching...";

        git_remote *git_remote = nullptr;
        error = git_remote_lookup(&git_remote, repository, remote.name.toLocal8Bit().constData());
        if (error < 0) {
            // If remote doesn't exist, add it
            error = git_remote_create(&git_remote, repository, remote.name.toLocal8Bit().constData(), remote.url.toLocal8Bit().constData());
            if (error < 0) {
                remote.status = "Error";
                failedRemotes.append(remote.name);
                allSuccessful = false;
                logMessage(QString("Failed to create remote %1: %2").arg(remote.name, getGitErrorMessage(error)));
                continue;
            }
        }

        // Fetch from remote
        git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;

        // Set up authentication callback for SSH
        fetch_opts.callbacks.credentials = [](git_credential **out, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload) -> int {
            FetchDeeznutzWindow *window = static_cast<FetchDeeznutzWindow*>(payload);
            return window->sshKeyCallback(out, url, username_from_url, allowed_types, payload);
        };
        fetch_opts.callbacks.payload = this;

        // Set up callbacks for better error reporting
        fetch_opts.callbacks.sideband_progress = [](const char *str, int len, void *payload) -> int {
            // Log progress messages
            QString message = QString::fromUtf8(str, len).trimmed();
            if (!message.isEmpty()) {
                // This would need access to the FetchDeeznutzWindow instance, but for now just return success
            }
            return 0;
        };

        fetch_opts.callbacks.transfer_progress = [](const git_indexer_progress *stats, void *payload) -> int {
            // Log transfer progress
            return 0;
        };

        // Try to fetch
        error = git_remote_fetch(git_remote, nullptr, &fetch_opts, nullptr);

        if (error < 0) {
            remote.status = "Error";
            failedRemotes.append(remote.name);
            allSuccessful = false;
            logMessage(QString("Failed to fetch from %1: %2").arg(remote.name, getGitErrorMessage(error)));
        } else {
            remote.status = "Success";
            remote.lastFetch = QDateTime::currentDateTime().toString(Qt::ISODate);
            logMessage(QString("✓ Successfully fetched from: %1").arg(remote.name));

            // Calculate commit counts after successful fetch
            calculateRemoteCommitCounts(repository, remote, repo.branch);
        }

        git_remote_free(git_remote);
    }

    git_repository_free(repository);

    if (allSuccessful) {
        repo.status = "Success";
        repo.lastFetch = QDateTime::currentDateTime().toString(Qt::ISODate);
        onFetchFinished();
    } else {
        repo.status = QString("Partial (%1/%2 failed)").arg(failedRemotes.size()).arg(repo.remotes.size());
        onFetchError(QString("Some remotes failed: %1").arg(failedRemotes.join(", ")));
    }
}


bool FetchDeeznutzWindow::isRepositoryValid(const QString& path)
{
    git_repository *repository = nullptr;
    int error = git_repository_open(&repository, path.toLocal8Bit().constData());

    if (error < 0) {
        return false;
    }

    git_repository_free(repository);
    return true;
}

QString FetchDeeznutzWindow::getGitErrorMessage(int error) const
{
    const git_error *git_error = git_error_last();
    if (git_error && git_error->message) {
        return QString::fromUtf8(git_error->message);
    }
    return QString("Unknown Git error: %1").arg(error);
}

int FetchDeeznutzWindow::sshKeyCallback(git_credential **out, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload)
{
    logMessage(QString("SSH authentication requested for URL: %1").arg(QString::fromUtf8(url)));
    logMessage(QString("Username: %1, Allowed types: %2").arg(QString::fromUtf8(username_from_url)).arg(allowed_types));

    // Try SSH key authentication first
    if (allowed_types & GIT_CREDENTIAL_SSH_KEY) {
        // Use SSH agent if available
        int error = git_credential_ssh_key_from_agent(out, username_from_url);
        if (error == 0) {
            logMessage("Using SSH key from SSH agent");
            return 0;
        }

        // Fall back to default SSH key locations
        QString homeDir = QDir::homePath();
        QStringList sshKeyPaths = {
            homeDir + "/.ssh/id_rsa",
            homeDir + "/.ssh/id_ed25519",
            homeDir + "/.ssh/id_ecdsa",
            homeDir + "/.ssh/id_dsa"
        };

        for (const QString& keyPath : sshKeyPaths) {
            if (QFile::exists(keyPath)) {
                error = git_credential_ssh_key_new(out, username_from_url, nullptr, keyPath.toLocal8Bit().constData(), nullptr);
                if (error == 0) {
                    logMessage(QString("Using SSH key: %1").arg(keyPath));
                    return 0;
                }
            }
        }
    }

    // Try username/password if allowed
    if (allowed_types & GIT_CREDENTIAL_USERPASS_PLAINTEXT) {
        logMessage("SSH key authentication failed, but username/password not supported for SSH URLs");
    }

    logMessage("No suitable authentication method found");
    return GIT_EUSER; // User cancelled
}

void FetchDeeznutzWindow::calculateCommitCounts(GitRepository& repo)
{
    if (!isRepositoryValid(repo.localPath)) {
        return;
    }

    git_repository *repository = nullptr;
    int error = git_repository_open(&repository, repo.localPath.toLocal8Bit().constData());
    if (error < 0) {
        return;
    }

    for (GitRemote& remote : repo.remotes) {
        calculateRemoteCommitCounts(repository, remote, repo.branch);
    }

    git_repository_free(repository);
    updateRepositoryTree();
}

void FetchDeeznutzWindow::calculateRemoteCommitCounts(git_repository* repository, GitRemote& remote, const QString& branch)
{
    // Get the local branch reference
    QString localBranchRef = QString("refs/heads/%1").arg(branch);
    git_reference *localBranch = nullptr;
    int error = git_reference_lookup(&localBranch, repository, localBranchRef.toLocal8Bit().constData());

    if (error < 0) {
        // Local branch doesn't exist, try HEAD
        error = git_repository_head(&localBranch, repository);
        if (error < 0) {
            remote.commitsAhead = 0;
            remote.commitsBehind = 0;
            return;
        }
    }

    // Get the remote branch reference
    QString remoteBranchRef = QString("refs/remotes/%1/%2").arg(remote.name, branch);
    git_reference *remoteBranch = nullptr;
    error = git_reference_lookup(&remoteBranch, repository, remoteBranchRef.toLocal8Bit().constData());

    if (error < 0) {
        // Remote branch doesn't exist, try default branch
        remoteBranchRef = QString("refs/remotes/%1/HEAD").arg(remote.name);
        error = git_reference_lookup(&remoteBranch, repository, remoteBranchRef.toLocal8Bit().constData());
        if (error < 0) {
            git_reference_free(localBranch);
            remote.commitsAhead = 0;
            remote.commitsBehind = 0;
            return;
        }
    }

    // Get the commits
    git_commit *localCommit = nullptr;
    git_commit *remoteCommit = nullptr;

    error = git_commit_lookup(&localCommit, repository, git_reference_target(localBranch));
    if (error < 0) {
        git_reference_free(localBranch);
        git_reference_free(remoteBranch);
        remote.commitsAhead = 0;
        remote.commitsBehind = 0;
        return;
    }

    error = git_commit_lookup(&remoteCommit, repository, git_reference_target(remoteBranch));
    if (error < 0) {
        git_commit_free(localCommit);
        git_reference_free(localBranch);
        git_reference_free(remoteBranch);
        remote.commitsAhead = 0;
        remote.commitsBehind = 0;
        return;
    }

    // Calculate ahead/behind counts
    git_revwalk *walk = nullptr;
    error = git_revwalk_new(&walk, repository);
    if (error >= 0) {
        git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL);

        // Count commits ahead (in local but not in remote)
        git_revwalk_push(walk, git_commit_id(localCommit));
        git_revwalk_hide(walk, git_commit_id(remoteCommit));

        int ahead = 0;
        git_oid oid;
        while (git_revwalk_next(&oid, walk) == 0) {
            ahead++;
        }

        // Count commits behind (in remote but not in local)
        git_revwalk_reset(walk);
        git_revwalk_push(walk, git_commit_id(remoteCommit));
        git_revwalk_hide(walk, git_commit_id(localCommit));

        int behind = 0;
        while (git_revwalk_next(&oid, walk) == 0) {
            behind++;
        }

        remote.commitsAhead = ahead;
        remote.commitsBehind = behind;

        git_revwalk_free(walk);
    } else {
        remote.commitsAhead = 0;
        remote.commitsBehind = 0;
    }

    git_commit_free(localCommit);
    git_commit_free(remoteCommit);
    git_reference_free(localBranch);
    git_reference_free(remoteBranch);
}

QTreeWidgetItem* FetchDeeznutzWindow::createPathTreeItem(const QString& path)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();
    item->setText(0, path);
    item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void*>(nullptr))); // Mark as path item
    return item;
}

QTreeWidgetItem* FetchDeeznutzWindow::findOrCreatePathItem(const QString& path)
{
    // Find existing path item
    for (int i = 0; i < repositoryTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = repositoryTree->topLevelItem(i);
        if (item->text(0) == path) {
            return item;
        }
    }

    // Create new path item
    QTreeWidgetItem* pathItem = createPathTreeItem(path);
    repositoryTree->addTopLevelItem(pathItem);
    return pathItem;
}

GitRepository* FetchDeeznutzWindow::getRepositoryFromTreeItem(QTreeWidgetItem* item)
{
    if (!item) return nullptr;

    QVariant data = item->data(0, Qt::UserRole);
    if (data.isValid()) {
        void* ptr = data.value<void*>();
        if (ptr) {
            return static_cast<GitRepository*>(ptr);
        }
    }
    return nullptr;
}

void FetchDeeznutzWindow::logMessage(const QString& message)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    logTextEdit->append(QString("[%1] %2").arg(timestamp, message));
}

QString FetchDeeznutzWindow::getConfigFilePath() const
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    return QDir(configDir).filePath("repositories.json");
}

void FetchDeeznutzWindow::loadRepositories()
{
    QFile file(getConfigFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        logMessage("No existing configuration found, starting fresh");
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonArray array = doc.array();

    repositories.clear();
    for (const QJsonValue& value : array) {
        if (value.isObject()) {
            repositories.append(GitRepository::fromJson(value.toObject()));
        }
    }

    logMessage(QString("Loaded %1 repositories from configuration").arg(repositories.size()));

    // Calculate commit counts for loaded repositories
    for (GitRepository& repo : repositories) {
        calculateCommitCounts(repo);
    }
}

void FetchDeeznutzWindow::saveRepositories()
{
    QJsonArray array;
    for (const GitRepository& repo : repositories) {
        array.append(repo.toJson());
    }

    QJsonDocument doc(array);
    QFile file(getConfigFilePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        logMessage("Configuration saved");
    } else {
        logMessage("Failed to save configuration");
    }
}

void FetchDeeznutzWindow::scanDirectoryForRepositories(const QString& directoryPath)
{
    QStringList excludeDirs = {".git", "node_modules", ".vscode", ".idea", "build", "dist", "target", "__pycache__"};
    QStringList gitRepos = findGitRepositories(directoryPath, excludeDirs);

    int addedCount = 0;
    int skippedCount = 0;

    for (const QString& repoPath : gitRepos) {
        // Check if repository already exists
        bool alreadyExists = false;
        for (const GitRepository& existingRepo : repositories) {
            if (existingRepo.localPath == repoPath) {
                alreadyExists = true;
                break;
            }
        }

        if (alreadyExists) {
            skippedCount++;
            continue;
        }

        // Create new repository entry
        GitRepository repo;
        repo.name = getRepositoryName(repoPath);
        repo.localPath = repoPath;
        repo.branch = getRepositoryBranch(repoPath);
        repo.fetchInterval = 60; // Default 1 hour
        repo.enabled = true;
        repo.status = "Ready";
        repo.remotes = getRepositoryRemotes(repoPath);

        if (!repo.name.isEmpty() && !repo.remotes.isEmpty()) {
            repositories.append(repo);
            addedCount++;
            logMessage(QString("Discovered repository: %1 at %2 with %3 remotes").arg(repo.name, repoPath).arg(repo.remotes.size()));
        } else {
            logMessage(QString("Skipped invalid repository at: %1 (no remotes found)").arg(repoPath));
        }
    }

    if (addedCount > 0) {
        // Calculate commit counts for newly added repositories
        for (int i = repositories.size() - addedCount; i < repositories.size(); ++i) {
            calculateCommitCounts(repositories[i]);
        }
        updateRepositoryTree();
        saveRepositories();
    }

    logMessage(QString("Directory scan complete: %1 repositories added, %2 skipped (already exist)").arg(addedCount).arg(skippedCount));
}

QStringList FetchDeeznutzWindow::findGitRepositories(const QString& directoryPath, const QStringList& excludeDirs)
{
    QStringList repositories;
    QDir dir(directoryPath);

    if (!dir.exists()) {
        return repositories;
    }

    // Check if current directory is a git repository
    if (isGitRepository(directoryPath)) {
        repositories.append(directoryPath);
        return repositories; // Don't recurse into subdirectories of a git repo
    }

    // Get all subdirectories
    QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QFileInfo& entry : entries) {
        QString subDirPath = entry.absoluteFilePath();
        QString dirName = entry.fileName();

        // Skip excluded directories
        if (excludeDirs.contains(dirName, Qt::CaseInsensitive)) {
            continue;
        }

        // Recursively search subdirectories
        repositories.append(findGitRepositories(subDirPath, excludeDirs));
    }

    return repositories;
}

bool FetchDeeznutzWindow::isGitRepository(const QString& path)
{
    QDir dir(path);
    return dir.exists(".git");
}

QString FetchDeeznutzWindow::getRepositoryName(const QString& path)
{
    QDir dir(path);
    return dir.dirName();
}

QList<GitRemote> FetchDeeznutzWindow::getRepositoryRemotes(const QString& path)
{
    QList<GitRemote> remotes;

    git_repository *repository = nullptr;
    int error = git_repository_open(&repository, path.toLocal8Bit().constData());

    if (error < 0) {
        return remotes;
    }

    git_strarray remote_names = {0};
    error = git_remote_list(&remote_names, repository);

    if (error >= 0) {
        for (size_t i = 0; i < remote_names.count; ++i) {
            git_remote *remote = nullptr;
            error = git_remote_lookup(&remote, repository, remote_names.strings[i]);

            if (error >= 0) {
                GitRemote gitRemote;
                gitRemote.name = QString::fromUtf8(remote_names.strings[i]);

                const char *remote_url = git_remote_url(remote);
                if (remote_url) {
                    gitRemote.url = QString::fromUtf8(remote_url);
                    gitRemote.status = "Ready";
                    remotes.append(gitRemote);
                }

                git_remote_free(remote);
            }
        }

        git_strarray_free(&remote_names);
    }

    git_repository_free(repository);
    return remotes;
}

QString FetchDeeznutzWindow::getRepositoryBranch(const QString& path)
{
    git_repository *repository = nullptr;
    int error = git_repository_open(&repository, path.toLocal8Bit().constData());

    if (error < 0) {
        return "main"; // Default branch
    }

    git_reference *head = nullptr;
    error = git_repository_head(&head, repository);

    QString branch = "main"; // Default
    if (error >= 0) {
        const char *branch_name = git_reference_shorthand(head);
        if (branch_name) {
            branch = QString::fromUtf8(branch_name);
        }
        git_reference_free(head);
    }

    git_repository_free(repository);
    return branch;
}

QString FetchDeeznutzWindow::generateRepositoryTooltip(const GitRepository& repo)
{
    QString tooltip = QString("<b>%1</b><br/>").arg(repo.name);
    tooltip += QString("Path: %1<br/>").arg(repo.localPath);
    tooltip += QString("Branch: %1<br/>").arg(repo.branch);
    tooltip += QString("Status: %1<br/>").arg(repo.status.isEmpty() ? "Ready" : repo.status);
    
    if (!repo.lastFetch.isEmpty()) {
        tooltip += QString("Last Fetch: %1<br/>").arg(repo.lastFetch);
    }
    
    tooltip += QString("Fetch Interval: %1 minutes<br/>").arg(repo.fetchInterval);
    tooltip += QString("Enabled: %1<br/><br/>").arg(repo.enabled ? "Yes" : "No");
    
    if (repo.remotes.isEmpty()) {
        tooltip += "<b>No remotes configured</b>";
    } else {
        tooltip += QString("<b>Remotes (%1):</b><br/>").arg(repo.remotes.size());
        for (const GitRemote& remote : repo.remotes) {
            tooltip += QString("• <b>%1</b><br/>").arg(remote.name);
            tooltip += QString("  URL: %1<br/>").arg(remote.url);
            tooltip += QString("  Status: %1<br/>").arg(remote.status.isEmpty() ? "Ready" : remote.status);
            
            if (remote.commitsAhead > 0 || remote.commitsBehind > 0) {
                tooltip += QString("  Commits: ");
                if (remote.commitsAhead > 0) {
                    tooltip += QString("+%1 ahead").arg(remote.commitsAhead);
                }
                if (remote.commitsAhead > 0 && remote.commitsBehind > 0) {
                    tooltip += ", ";
                }
                if (remote.commitsBehind > 0) {
                    tooltip += QString("-%1 behind").arg(remote.commitsBehind);
                }
                tooltip += "<br/>";
            }
            
            if (!remote.lastFetch.isEmpty()) {
                tooltip += QString("  Last Fetch: %1<br/>").arg(remote.lastFetch);
            }
            tooltip += "<br/>";
        }
    }
    
    return tooltip;
}

void FetchDeeznutzWindow::onBackgroundFetchStarted(const QString& repoName)
{
    logMessage(QString("🔄 Started fetching: %1").arg(repoName));
    
    // Create progress bar for this repository
    QProgressBar *progressBar = new QProgressBar();
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setFormat(QString("%1 - Starting...").arg(repoName));
    
    QLabel *label = new QLabel(repoName);
    QHBoxLayout *itemLayout = new QHBoxLayout();
    itemLayout->addWidget(label);
    itemLayout->addWidget(progressBar);
    
    QWidget *itemWidget = new QWidget();
    itemWidget->setLayout(itemLayout);
    
    activeFetches[repoName] = progressBar;
    fetchStatusLayout->addWidget(itemWidget);
    fetchStatusGroup->setVisible(true);
    
    // Update repository status in tree
    for (GitRepository& repo : repositories) {
        if (repo.name == repoName) {
            repo.status = "Fetching...";
            break;
        }
    }
    updateRepositoryTree();
}

void FetchDeeznutzWindow::onBackgroundFetchProgress(const QString& repoName, const QString& remoteName, int progress)
{
    if (activeFetches.contains(repoName)) {
        QProgressBar *progressBar = activeFetches[repoName];
        progressBar->setValue(progress);
        progressBar->setFormat(QString("%1 - %2 (%3%)").arg(repoName, remoteName).arg(progress));
    }
}

void FetchDeeznutzWindow::onBackgroundFetchFinished(const QString& repoName, bool success, const QString& message)
{
    logMessage(QString("✅ Finished fetching: %1 - %2").arg(repoName, message));
    
    // Remove progress bar
    if (activeFetches.contains(repoName)) {
        QProgressBar *progressBar = activeFetches[repoName];
        QWidget *itemWidget = progressBar->parentWidget();
        if (itemWidget) {
            fetchStatusLayout->removeWidget(itemWidget);
            itemWidget->deleteLater();
        }
        activeFetches.remove(repoName);
    }
    
    // Hide fetch status group if no active fetches
    if (activeFetches.isEmpty()) {
        fetchStatusGroup->setVisible(false);
    }
    
    // Update repository status in tree
    for (GitRepository& repo : repositories) {
        if (repo.name == repoName) {
            repo.status = success ? "Success" : "Error";
            repo.lastFetch = QDateTime::currentDateTime().toString(Qt::ISODate);
            break;
        }
    }
    updateRepositoryTree();
    saveRepositories();
}

void FetchDeeznutzWindow::onBackgroundFetchError(const QString& repoName, const QString& errorMessage)
{
    // Check if this is a timeout error
    bool isTimeout = errorMessage.contains("timed out", Qt::CaseInsensitive);
    QString logIcon = isTimeout ? "⏰" : "❌";
    QString statusText = isTimeout ? "Timeout" : "Error";
    
    logMessage(QString("%1 %2 fetching: %3 - %4").arg(logIcon, statusText, repoName, errorMessage));
    
    // Remove progress bar
    if (activeFetches.contains(repoName)) {
        QProgressBar *progressBar = activeFetches[repoName];
        QWidget *itemWidget = progressBar->parentWidget();
        if (itemWidget) {
            fetchStatusLayout->removeWidget(itemWidget);
            itemWidget->deleteLater();
        }
        activeFetches.remove(repoName);
    }
    
    // Hide fetch status group if no active fetches
    if (activeFetches.isEmpty()) {
        fetchStatusGroup->setVisible(false);
    }
    
    // Update repository status in tree
    for (GitRepository& repo : repositories) {
        if (repo.name == repoName) {
            repo.status = statusText;
            break;
        }
    }
    updateRepositoryTree();
    saveRepositories();
}


