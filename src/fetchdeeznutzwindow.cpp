#include "fetchdeeznutzwindow.h"
#include <QApplication>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QIcon>
#include <QTextStream>
#include <QContextMenuEvent>
#include <QFileInfo>
#include <QMap>
#include <QMetaType>
#include <QMutexLocker>
#include <QtConcurrent>
#include <git2.h>

namespace {
// Stable identity for a repository, used as tree-item data. We deliberately
// avoid storing raw pointers into the `repositories` QList: those dangle the
// moment the list reallocates (append) or elements shift (erase). A name+path
// key survives list mutations and lets us re-resolve the current element.
QString repositoryKey(const QString& name, const QString& localPath)
{
    return name + QLatin1Char('\x1f') + localPath;
}
} // namespace

FetchDeeznutzWindow::FetchDeeznutzWindow(QWidget *parent)
    : QMainWindow(parent)
    , fetchTimer(new QTimer(this))
    , fetchThread(new QThread(this))
    , fetchWorker(new GitFetchWorker())
    , currentFetchIndex(-1)
    , isFetching(false)
{
    // Initialize libgit2 FIRST - before any git operations
    git_libgit2_init();

    // Concurrent fetches rely on libgit2 being thread-safe across independent
    // repository handles, which requires it to be built with thread support.
    if (!(git_libgit2_features() & GIT_FEATURE_THREADS)) {
        qWarning() << "libgit2 was built without thread support; concurrent git "
                      "operations may be unsafe.";
    }
    
    setWindowTitle("Git Repository Fetcher");
    setMinimumSize(800, 600);

    // Register types with Qt's meta-object system (must be done before moving to thread)
    qRegisterMetaType<GitRemote>("GitRemote");
    qRegisterMetaType<GitRepository>("GitRepository");
    
    // Verify registration (using QMetaType::fromType for Qt6 compatibility)
    if (!QMetaType::fromType<GitRepository>().isValid()) {
        qWarning() << "Failed to register GitRepository meta type";
    }
    if (!QMetaType::fromType<GitRemote>().isValid()) {
        qWarning() << "Failed to register GitRemote meta type";
    }
    
    // Setup background thread and worker
    fetchWorker->moveToThread(fetchThread);
    connect(fetchThread, &QThread::finished, fetchWorker, &GitFetchWorker::deleteLater);
    connect(fetchWorker, &GitFetchWorker::fetchStarted, this, &FetchDeeznutzWindow::onBackgroundFetchStarted);
    connect(fetchWorker, &GitFetchWorker::fetchProgress, this, &FetchDeeznutzWindow::onBackgroundFetchProgress);
    connect(fetchWorker, &GitFetchWorker::fetchFinished, this, &FetchDeeznutzWindow::onBackgroundFetchFinished);
    connect(fetchWorker, &GitFetchWorker::fetchError, this, &FetchDeeznutzWindow::onBackgroundFetchError);
    connect(fetchWorker, &GitFetchWorker::commitCountsUpdated, this, &FetchDeeznutzWindow::onCommitCountsUpdated);
    fetchThread->start();
    
    // Initial timeout values will be set in loadSettings()

    setupUI();
    setupSystemTray();
    loadSettings(); // Load settings before loading repositories
    loadRepositories();
    updateRepositoryTree();

    // Connect timer for scheduled fetching
    connect(fetchTimer, &QTimer::timeout, this, &FetchDeeznutzWindow::performScheduledFetch);

    // Start timer based on loaded settings
    if (autoFetchCheckBox->isChecked()) {
        fetchTimer->start(globalIntervalSpinBox->value() * 60000);
    }
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
    repositoryTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(repositoryTree, &QTreeWidget::itemSelectionChanged, this, &FetchDeeznutzWindow::onRepositorySelectionChanged);
    connect(repositoryTree, &QTreeWidget::customContextMenuRequested, this, &FetchDeeznutzWindow::showContextMenu);
    connect(repositoryTree, &QTreeWidget::itemDoubleClicked, this, &FetchDeeznutzWindow::onRepositoryItemDoubleClicked);
    repoLayout->addWidget(repositoryTree);

    // Setup context menu
    contextMenu = new QMenu(this);
    QAction *removeRepoAction = new QAction("Remove Repository", this);
    QAction *removeDirAction = new QAction("Remove Directory", this);
    contextMenu->addAction(removeRepoAction);
    contextMenu->addAction(removeDirAction);
    connect(removeRepoAction, &QAction::triggered, this, &FetchDeeznutzWindow::removeRepository);
    connect(removeDirAction, &QAction::triggered, this, &FetchDeeznutzWindow::removeDirectory);

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
    
    // Initially update the enabled state of interval controls (will be updated again in loadSettings)
    updateAutoFetchControls();

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

void FetchDeeznutzWindow::setupSystemTray()
{
    // Create system tray icon
    trayIcon = new QSystemTrayIcon(this);
    
    // Set custom nuts icon
    trayIcon->setIcon(QIcon(":/nuts_icon.svg"));
    
    // Create tray menu
    trayMenu = new QMenu(this);
    
    showAction = new QAction("Show", this);
    hideAction = new QAction("Hide", this);
    quitAction = new QAction("Quit", this);
    
    trayMenu->addAction(showAction);
    trayMenu->addAction(hideAction);
    trayMenu->addSeparator();
    trayMenu->addAction(quitAction);
    
    trayIcon->setContextMenu(trayMenu);
    
    // Connect signals
    connect(trayIcon, &QSystemTrayIcon::activated, this, &FetchDeeznutzWindow::onTrayIconActivated);
    connect(showAction, &QAction::triggered, this, &FetchDeeznutzWindow::showWindow);
    connect(hideAction, &QAction::triggered, this, &FetchDeeznutzWindow::hideWindow);
    connect(quitAction, &QAction::triggered, this, &FetchDeeznutzWindow::quitApplication);
    
    // Show tray icon
    trayIcon->show();
    
    // Set tooltip
    trayIcon->setToolTip("FetchDeezNutz - Git Repository Manager");
}

void FetchDeeznutzWindow::addRepository()
{
    RepositoryDialog dialog(GitRepository(), this);
    if (dialog.exec() == QDialog::Accepted) {
        GitRepository repo = dialog.getRepository();
        if (!repo.name.isEmpty() && !repo.remotes.isEmpty()) {
            // If repository has multiple remotes, show selection dialog
            if (repo.remotes.size() > 1) {
                RemoteSelectionDialog remoteDialog(repo.remotes, this);
                if (remoteDialog.exec() == QDialog::Accepted) {
                    QList<GitRemote> selectedRemotes = remoteDialog.getSelectedRemotes();
                    if (selectedRemotes.isEmpty()) {
                        QMessageBox::warning(this, "No Remotes Selected", "Please select at least one remote to monitor.");
                        return;
                    }
                    repo.remotes = selectedRemotes;
                } else {
                    return; // User cancelled remote selection
                }
            }
            
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

void FetchDeeznutzWindow::removeDirectory()
{
    QTreeWidgetItem* currentItem = repositoryTree->currentItem();
    if (!currentItem) return;

    // Check if this is a directory item (has no repository data)
    GitRepository* repo = getRepositoryFromTreeItem(currentItem);
    if (repo) {
        // If it's a repository item, remove just that repository
        removeRepository();
        return;
    }

    // It's a directory item, get the directory path
    QString dirPath = currentItem->text(0);
    
    // Count repositories in this directory
    int repoCount = 0;
    for (const GitRepository& repo : repositories) {
        QString repoDirPath = QFileInfo(repo.localPath).absolutePath();
        if (repoDirPath == dirPath) {
            repoCount++;
        }
    }

    if (repoCount == 0) {
        QMessageBox::information(this, "No Repositories", "No repositories found in this directory.");
        return;
    }

    int ret = QMessageBox::question(this, "Remove Directory",
                                   QString("Are you sure you want to remove all %1 repositories from directory '%2'?").arg(repoCount).arg(dirPath),
                                   QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        // Remove all repositories in this directory
        auto it = repositories.begin();
        while (it != repositories.end()) {
            QString repoDirPath = QFileInfo(it->localPath).absolutePath();
            if (repoDirPath == dirPath) {
                logMessage(QString("Removed repository: %1").arg(it->name));
                it = repositories.erase(it);
            } else {
                ++it;
            }
        }
        
        updateRepositoryTree();
        saveRepositories();
        logMessage(QString("Removed all repositories from directory: %1").arg(dirPath));
    }
}

void FetchDeeznutzWindow::showContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = repositoryTree->itemAt(pos);
    if (!item) return;

    // Get the actions from the context menu
    QList<QAction*> actions = contextMenu->actions();
    QAction* removeRepoAction = actions[0]; // "Remove Repository"
    QAction* removeDirAction = actions[1];  // "Remove Directory"

    // Check if this is a repository item or directory item
    GitRepository* repo = getRepositoryFromTreeItem(item);
    if (repo) {
        // It's a repository item
        removeRepoAction->setVisible(true);
        removeDirAction->setVisible(false);
        removeRepoAction->setText("Remove Repository");
    } else {
        // It's a directory item
        removeRepoAction->setVisible(false);
        removeDirAction->setVisible(true);
        removeDirAction->setText("Remove Directory");
    }

    // Show the context menu
    contextMenu->exec(repositoryTree->mapToGlobal(pos));
}

void FetchDeeznutzWindow::fetchSelected()
{
    if (!fetchWorker) {
        logMessage("Error: Fetch worker is not initialized");
        return;
    }
    
    QTreeWidgetItem* currentItem = repositoryTree->currentItem();
    GitRepository* repo = getRepositoryFromTreeItem(currentItem);
    if (repo) {
        // Make a copy to ensure thread safety
        GitRepository repoCopy = *repo;
        // Use background worker for fetching
        QMetaObject::invokeMethod(fetchWorker, "fetchRepository", Qt::QueuedConnection, Q_ARG(GitRepository, repoCopy));
    }
}

void FetchDeeznutzWindow::fetchAll()
{
    if (!fetchWorker) {
        logMessage("Error: Fetch worker is not initialized");
        return;
    }
    
    if (!fetchThread || !fetchThread->isRunning()) {
        logMessage("Error: Fetch worker thread is not running");
        return;
    }
    
    logMessage("Starting fetch for all enabled repositories...");
    for (const GitRepository& repo : repositories) {
        if (repo.enabled) {
            // Make an explicit copy to ensure thread safety
            GitRepository repoCopy = repo;
            // Use QMetaObject::invokeMethod with queued connection
            // This ensures the call happens in the worker thread
            QMetaObject::invokeMethod(fetchWorker, "fetchRepository", Qt::QueuedConnection, Q_ARG(GitRepository, repoCopy));
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

void FetchDeeznutzWindow::onRepositoryItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    
    if (!item) return;
    
    // Get the repository from the item
    GitRepository* repo = getRepositoryFromTreeItem(item);
    if (!repo) return;
    
    // Only handle repository items (not path items or remote items)
    // A repository item is one that has a parent (path item) but is not a remote item
    QTreeWidgetItem* parent = item->parent();
    if (!parent) return; // Top-level path item
    
    // Check if this is a remote item (has a repository as parent)
    QTreeWidgetItem* grandparent = parent->parent();
    if (grandparent) return; // This is a remote item, not a repository item
    
    // Find the tracking remote (the one with commitsBehind > 0 and can be fast-forwarded)
    GitRemote* trackingRemote = nullptr;
    for (GitRemote& remote : repo->remotes) {
        if (remote.commitsBehind > 0 && remote.commitsAhead == 0) {
            // Check if it can be fast-forwarded
            if (GitUtils::canFastForward(repo->localPath, repo->branch, remote.name)) {
                trackingRemote = &remote;
                break;
            }
        }
    }
    
    if (!trackingRemote) {
        logMessage(QString("Repository %1: No fast-forwardable remote branch found").arg(repo->name));
        return;
    }

    // This moves the local branch and working tree to the remote tip, so confirm
    // before performing what is a potentially surprising, mutating operation.
    const int ret = QMessageBox::question(
        this,
        "Fast-forward branch",
        QString("Fast-forward %1/%2 to %3/%2?\n\nThis updates your local branch and "
                "working tree to match the remote.")
            .arg(repo->name, repo->branch, trackingRemote->name),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        return;
    }

    logMessage(QString("Fast-forwarding %1/%2 to %3...").arg(repo->name, repo->branch, trackingRemote->name));
    QString errorMessage;
    if (GitUtils::rebaseBranch(repo->localPath, repo->branch, trackingRemote->name, errorMessage)) {
        logMessage(QString("✓ Successfully fast-forwarded %1/%2 to %3").arg(repo->name, repo->branch, trackingRemote->name));
        // Recalculate commit counts after the update
        calculateCommitCounts(*repo);
        updateRepositoryTree();
    } else {
        logMessage(QString("✗ Failed to fast-forward %1/%2: %3").arg(repo->name, repo->branch, errorMessage));
    }
}

void FetchDeeznutzWindow::onFetchIntervalChanged()
{
    saveSettings(); // Save settings when changed
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
    saveSettings(); // Save settings when changed
    int timeoutSeconds = connectionTimeoutSpinBox->value();
    QMetaObject::invokeMethod(fetchWorker, "setConnectionTimeout", Qt::QueuedConnection, Q_ARG(int, timeoutSeconds));
    logMessage(QString("Connection timeout changed to %1 seconds").arg(timeoutSeconds));
}

void FetchDeeznutzWindow::onAutoFetchToggled()
{
    saveSettings(); // Save settings when changed
    updateAutoFetchControls(); // Update enabled state of interval controls
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

void FetchDeeznutzWindow::onBackgroundFetchStarted(const QString& repoName)
{
    logMessage(QString("Started fetching: %1").arg(repoName));
    
    // Find the repository and update its status
    for (GitRepository& repo : repositories) {
        if (repo.name == repoName) {
            repo.status = "Fetching...";
            updateRepositoryTree();
            break;
        }
    }
}

void FetchDeeznutzWindow::onBackgroundFetchProgress(const QString& repoName, const QString& remoteName, int progress)
{
    // Update progress if needed
    Q_UNUSED(repoName);
    Q_UNUSED(remoteName);
    Q_UNUSED(progress);
}

void FetchDeeznutzWindow::onBackgroundFetchFinished(const QString& repoName, bool success, const QString& message)
{
    logMessage(QString("%1 %2: %3").arg(success ? "✓" : "✗", repoName, message));
    
    // Find the repository and update its status
    for (GitRepository& repo : repositories) {
        if (repo.name == repoName) {
            repo.status = success ? "Success" : "Error";
            if (success) {
                repo.lastFetch = QDateTime::currentDateTime().toString(Qt::ISODate);
            }
            updateRepositoryTree();
            saveRepositories();
            break;
        }
    }
}

void FetchDeeznutzWindow::onBackgroundFetchError(const QString& repoName, const QString& errorMessage)
{
    logMessage(QString("✗ Error fetching %1: %2").arg(repoName, errorMessage));
    
    // Find the repository and update its status
    for (GitRepository& repo : repositories) {
        if (repo.name == repoName) {
            repo.status = "Error";
            updateRepositoryTree();
            saveRepositories();
            break;
        }
    }
}

void FetchDeeznutzWindow::onCommitCountsUpdated(const QString& repoName, const QString& remoteName, int commitsAhead, int commitsBehind)
{
    // Find the repository and update the remote's commit counts
    for (GitRepository& repo : repositories) {
        if (repo.name == repoName) {
            for (GitRemote& remote : repo.remotes) {
                if (remote.name == remoteName) {
                    remote.commitsAhead = commitsAhead;
                    remote.commitsBehind = commitsBehind;
                    updateRepositoryTree();
                    break;
                }
            }
            break;
        }
    }
}

void FetchDeeznutzWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    // Handle both single click (Trigger) and double click
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        if (isVisible() && !isMinimized()) {
            hide();
        } else {
            setWindowState(Qt::WindowNoState);
            show();
            raise();
            activateWindow();
        }
    }
}

void FetchDeeznutzWindow::showWindow()
{
    setWindowState(Qt::WindowNoState);
    show();
    raise();
    activateWindow();
}

void FetchDeeznutzWindow::hideWindow()
{
    hide();
}

void FetchDeeznutzWindow::quitApplication()
{
    QApplication::quit();
}

void FetchDeeznutzWindow::closeEvent(QCloseEvent *event)
{
    // Hide to system tray instead of closing
    hide();
    event->ignore();
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
            
            // Repository item text
            QString itemText = QString("%1 %2 - %3 (%4) [%5 remotes]")
                              .arg(statusIcon)
                              .arg(repo->name)
                              .arg(statusText)
                              .arg(repo->branch)
                              .arg(repo->remotes.size());

            QTreeWidgetItem* repoItem = new QTreeWidgetItem(pathItem);
            repoItem->setText(0, itemText);
            repoItem->setData(0, Qt::UserRole, repositoryKey(repo->name, repo->localPath));
            
            // Set tooltip for the repository item
            QString tooltip = generateRepositoryTooltip(*repo);
            repoItem->setToolTip(0, tooltip);
            
            // Add each remote as a child item with its own commit counts
            for (const GitRemote& remote : repo->remotes) {
                QString remoteStatusIcon = "●";
                QString remoteStatus = remote.status.isEmpty() ? "Ready" : remote.status;
                
                if (remoteStatus == "Error") {
                    remoteStatusIcon = "❌";
                } else if (remoteStatus == "Success") {
                    remoteStatusIcon = "✅";
                } else if (remoteStatus == "Fetching...") {
                    remoteStatusIcon = "🔄";
                }
                
                QString remoteCommitDeltaText;
                if (remote.commitsAhead > 0 && remote.commitsBehind > 0) {
                    remoteCommitDeltaText = QString(" [+%1/-%2]").arg(remote.commitsAhead).arg(remote.commitsBehind);
                } else if (remote.commitsAhead > 0) {
                    remoteCommitDeltaText = QString(" [+%1]").arg(remote.commitsAhead);
                } else if (remote.commitsBehind > 0) {
                    remoteCommitDeltaText = QString(" [-%1]").arg(remote.commitsBehind);
                } else {
                    remoteCommitDeltaText = " [up-to-date]";
                }
                
                QString remoteItemText = QString("%1 %2 - %3%4")
                                        .arg(remoteStatusIcon)
                                        .arg(remote.name)
                                        .arg(remote.url)
                                        .arg(remoteCommitDeltaText);
                
                QTreeWidgetItem* remoteItem = new QTreeWidgetItem(repoItem);
                remoteItem->setText(0, remoteItemText);
                // Store the repository key so we can resolve the repo from remote items
                remoteItem->setData(0, Qt::UserRole, repositoryKey(repo->name, repo->localPath));
                
                // Set tooltip with remote details
                QString remoteTooltip = QString("Remote: %1\nURL: %2\nStatus: %3\nAhead: %4 commits\nBehind: %5 commits")
                                       .arg(remote.name)
                                       .arg(remote.url)
                                       .arg(remoteStatus)
                                       .arg(remote.commitsAhead)
                                       .arg(remote.commitsBehind);
                if (!remote.lastFetch.isEmpty()) {
                    remoteTooltip += QString("\nLast fetch: %1").arg(remote.lastFetch);
                }
                remoteItem->setToolTip(0, remoteTooltip);
            }
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
    // This method is kept for compatibility but now uses the background worker
    if (!fetchWorker) {
        logMessage("Error: Fetch worker is not initialized");
        return;
    }
    
    GitRepository repoCopy = repo;
    QMetaObject::invokeMethod(fetchWorker, "fetchRepository", Qt::QueuedConnection, Q_ARG(GitRepository, repoCopy));
}

void FetchDeeznutzWindow::calculateCommitCounts(GitRepository& repo)
{
    if (!GitUtils::isRepositoryValid(repo.localPath)) {
        return;
    }

    git_repository *repository = nullptr;
    int error;
    {
        QMutexLocker locker(&g_gitMutex);
        error = git_repository_open(&repository, repo.localPath.toLocal8Bit().constData());
    }
    if (error < 0) {
        return;
    }

    for (GitRemote& remote : repo.remotes) {
        GitUtils::calculateRemoteCommitCounts(repository, remote, repo.branch, repo.name);
    }

    {
        QMutexLocker locker(&g_gitMutex);
        git_repository_free(repository);
    }
    updateRepositoryTree();
}

void FetchDeeznutzWindow::calculateCommitCountsAsync(const GitRepository& repo)
{
    // Capture an immutable snapshot of everything the background thread needs.
    // The worker never touches the shared `repositories` list; it computes into
    // local copies and posts the results back to the main thread, which owns the
    // list. This avoids data races and dangling pointers if the list is mutated
    // (add/remove/scan) while the calculation is running.
    const QString repoName = repo.name;
    const QString repoPath = repo.localPath;
    const QString branch = repo.branch;

    QStringList remoteNames;
    remoteNames.reserve(repo.remotes.size());
    for (const GitRemote& remote : repo.remotes) {
        remoteNames.append(remote.name);
    }

    [[maybe_unused]] QFuture<void> future = QtConcurrent::run([this, repoName, repoPath, branch, remoteNames]() {
        if (!GitUtils::isRepositoryValid(repoPath)) {
            return;
        }

        git_repository *repository = nullptr;
        int error;
        {
            QMutexLocker locker(&g_gitMutex);
            error = git_repository_open(&repository, repoPath.toLocal8Bit().constData());
        }
        if (error < 0) {
            return;
        }

        for (const QString& remoteName : remoteNames) {
            GitRemote remote;
            remote.name = remoteName;
            GitUtils::calculateRemoteCommitCounts(repository, remote, branch, repoName);

            const int ahead = remote.commitsAhead;
            const int behind = remote.commitsBehind;
            // Deliver results to the main thread, which owns `repositories`.
            QMetaObject::invokeMethod(this, [this, repoName, remoteName, ahead, behind]() {
                onCommitCountsUpdated(repoName, remoteName, ahead, behind);
            }, Qt::QueuedConnection);
        }

        {
            QMutexLocker locker(&g_gitMutex);
            git_repository_free(repository);
        }
    });
}

void FetchDeeznutzWindow::scanDirectoryForRepositories(const QString& directoryPath)
{
    QStringList excludeDirs = {".git", "node_modules", ".vscode", ".idea", "build", "dist", "target", "__pycache__"};
    QStringList gitRepos = GitUtils::findGitRepositories(directoryPath, excludeDirs);

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
        repo.name = GitUtils::getRepositoryName(repoPath);
        repo.localPath = repoPath;
        repo.branch = GitUtils::getRepositoryBranch(repoPath);
        repo.fetchInterval = 60; // Default 1 hour
        repo.enabled = true;
        repo.status = "Ready";
        repo.remotes = GitUtils::getRepositoryRemotes(repoPath);
        repo.worktrees = GitUtils::findWorktreesForRepository(repoPath);

        if (!repo.name.isEmpty() && !repo.remotes.isEmpty()) {
            // If repository has multiple remotes, show selection dialog
            if (repo.remotes.size() > 1) {
                RemoteSelectionDialog remoteDialog(repo.remotes, this);
                remoteDialog.setWindowTitle(QString("Select Remotes for %1").arg(repo.name));
                if (remoteDialog.exec() == QDialog::Accepted) {
                    QList<GitRemote> selectedRemotes = remoteDialog.getSelectedRemotes();
                    if (!selectedRemotes.isEmpty()) {
                        repo.remotes = selectedRemotes;
                    } else {
                        logMessage(QString("Skipped repository %1: no remotes selected").arg(repo.name));
                        continue;
                    }
                } else {
                    logMessage(QString("Skipped repository %1: user cancelled remote selection").arg(repo.name));
                    continue;
                }
            }
            
            repositories.append(repo);
            addedCount++;
            QString worktreeInfo = repo.worktrees.isEmpty() ? "" : QString(" and %1 worktrees").arg(repo.worktrees.size());
            logMessage(QString("Discovered repository: %1 at %2 with %3 remotes%4").arg(repo.name, repoPath).arg(repo.remotes.size()).arg(worktreeInfo));
        } else {
            logMessage(QString("Skipped invalid repository at: %1 (no remotes found)").arg(repoPath));
        }
    }

    if (addedCount > 0) {
        // Calculate commit counts for newly added repositories asynchronously
        for (int i = repositories.size() - addedCount; i < repositories.size(); ++i) {
            calculateCommitCountsAsync(repositories[i]);
        }
        updateRepositoryTree();
        saveRepositories();
    }

    logMessage(QString("Directory scan complete: %1 repositories added, %2 skipped (already exist)").arg(addedCount).arg(skippedCount));
}

QTreeWidgetItem* FetchDeeznutzWindow::createPathTreeItem(const QString& path)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();
    item->setText(0, path);
    // Directory items carry no UserRole data; that's how they're distinguished
    // from repository/remote items (which store a repository key).
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

    const QVariant data = item->data(0, Qt::UserRole);
    if (data.isValid() && data.metaType().id() == QMetaType::QString) {
        const QString key = data.toString();
        if (!key.isEmpty()) {
            // Re-resolve against the live list; returns nullptr if the repo was
            // removed since the tree was last built (stale key).
            for (GitRepository& repo : repositories) {
                if (repositoryKey(repo.name, repo.localPath) == key) {
                    return &repo;
                }
            }
            return nullptr;
        }
    }

    // No key on this item (e.g. a directory item). Remote items normally carry
    // their own key, but fall back to the parent just in case.
    if (QTreeWidgetItem* parent = item->parent()) {
        return getRepositoryFromTreeItem(parent);
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

void FetchDeeznutzWindow::loadSettings()
{
    QSettings settings;
    
    // Load global interval (default: 60 minutes)
    int interval = settings.value("globalInterval", 60).toInt();
    globalIntervalSpinBox->setValue(interval);
    
    // Load fetch timeout (default: 300 seconds = 5 minutes)
    int fetchTimeout = settings.value("fetchTimeout", 300).toInt();
    fetchTimeoutSpinBox->setValue(fetchTimeout);
    QMetaObject::invokeMethod(fetchWorker, "setTimeout", Qt::QueuedConnection, Q_ARG(int, fetchTimeout));
    
    // Load connection timeout (default: 5 seconds)
    int connectionTimeout = settings.value("connectionTimeout", 5).toInt();
    connectionTimeoutSpinBox->setValue(connectionTimeout);
    QMetaObject::invokeMethod(fetchWorker, "setConnectionTimeout", Qt::QueuedConnection, Q_ARG(int, connectionTimeout));
    
    // Load auto-fetch enabled state (default: true)
    bool autoFetch = settings.value("autoFetchEnabled", true).toBool();
    autoFetchCheckBox->setChecked(autoFetch);
    
    // Update enabled state of interval controls based on auto-fetch setting
    updateAutoFetchControls();
}

void FetchDeeznutzWindow::saveSettings()
{
    QSettings settings;
    
    settings.setValue("globalInterval", globalIntervalSpinBox->value());
    settings.setValue("fetchTimeout", fetchTimeoutSpinBox->value());
    settings.setValue("connectionTimeout", connectionTimeoutSpinBox->value());
    settings.setValue("autoFetchEnabled", autoFetchCheckBox->isChecked());
    
    settings.sync();
}

void FetchDeeznutzWindow::updateAutoFetchControls()
{
    bool autoFetchEnabled = autoFetchCheckBox->isChecked();
    globalIntervalSpinBox->setEnabled(autoFetchEnabled);
    fetchTimeoutSpinBox->setEnabled(autoFetchEnabled);
    connectionTimeoutSpinBox->setEnabled(autoFetchEnabled);
}

void FetchDeeznutzWindow::loadRepositories()
{
    QString configPath = getConfigFilePath();
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        logMessage("No existing configuration found, starting fresh");
        return;
    }

    QByteArray data = file.readAll();
    file.close();
    
    if (data.isEmpty()) {
        logMessage("Configuration file is empty, starting fresh");
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        logMessage(QString("Failed to parse configuration file: %1 at offset %2")
                   .arg(parseError.errorString())
                   .arg(parseError.offset));
        logMessage("Configuration file may be corrupted. Starting fresh.");
        // Backup the corrupted file
        QString backupPath = configPath + ".corrupted." + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        QFile::copy(configPath, backupPath);
        logMessage(QString("Corrupted file backed up to: %1").arg(backupPath));
        return;
    }
    
    if (!doc.isArray()) {
        logMessage("Configuration file format is invalid (expected array), starting fresh");
        return;
    }
    
    QJsonArray array = doc.array();

    repositories.clear();
    for (const QJsonValue& value : array) {
        if (value.isObject()) {
            try {
                repositories.append(GitRepository::fromJson(value.toObject()));
            } catch (...) {
                logMessage("Warning: Skipped invalid repository entry in configuration");
            }
        }
    }

    logMessage(QString("Loaded %1 repositories from configuration").arg(repositories.size()));

    // Calculate commit counts for loaded repositories asynchronously
    for (GitRepository& repo : repositories) {
        calculateCommitCountsAsync(repo);
    }
}

void FetchDeeznutzWindow::saveRepositories()
{
    QJsonArray array;
    for (const GitRepository& repo : repositories) {
        array.append(repo.toJson());
    }

    QJsonDocument doc(array);
    QString configPath = getConfigFilePath();
    QString tempPath = configPath + ".tmp";
    
    // Write to temporary file first to avoid corruption if crash occurs
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly)) {
        logMessage("Failed to save configuration: cannot create temporary file");
        return;
    }
    
    QByteArray jsonData = doc.toJson();
    if (tempFile.write(jsonData) != jsonData.size()) {
        logMessage("Failed to save configuration: write error");
        tempFile.remove();
        return;
    }
    
    if (!tempFile.flush()) {
        logMessage("Failed to save configuration: flush error");
        tempFile.remove();
        return;
    }
    tempFile.close();
    
    // Atomically replace the original file with the temporary file
    QFile oldFile(configPath);
    if (oldFile.exists()) {
        oldFile.remove();
    }
    
    if (!QFile::rename(tempPath, configPath)) {
        logMessage("Failed to save configuration: cannot replace file");
        QFile::remove(tempPath);
        return;
    }
    
    logMessage("Configuration saved");
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
        }
    }
    
    return tooltip;
}
