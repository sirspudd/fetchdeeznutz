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
#include <QScrollBar>
#include <QItemSelectionModel>
#include <git2.h>

FetchDeeznutzWindow::FetchDeeznutzWindow(QWidget *parent)
    : QMainWindow(parent)
    , fetchTimer(new QTimer(this))
    , fetchTicker(new QTimer(this))
    , fetchThread(new QThread(this))
    , fetchWorker(new GitFetchWorker())
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
    connect(fetchWorker, &GitFetchWorker::remoteStatusChanged, this, &FetchDeeznutzWindow::onRemoteStatusChanged);
    connect(fetchWorker, &GitFetchWorker::fetchFinished, this, &FetchDeeznutzWindow::onBackgroundFetchFinished);
    connect(fetchWorker, &GitFetchWorker::fetchError, this, &FetchDeeznutzWindow::onBackgroundFetchError);
    connect(fetchWorker, &GitFetchWorker::commitCountsUpdated, this, &FetchDeeznutzWindow::onCommitCountsUpdated);
    connect(fetchWorker, &GitFetchWorker::newTagsFound, this, &FetchDeeznutzWindow::onNewTagsFound);
    fetchThread->start();
    
    // Initial timeout values will be set in loadSettings()

    setupUI();
    setupSystemTray();
    loadSettings(); // Load settings before loading repositories
    loadRepositories();
    updateRepositoryTree();

    // Connect timer for scheduled fetching
    connect(fetchTimer, &QTimer::timeout, this, &FetchDeeznutzWindow::performScheduledFetch);

    // 1s heartbeat that animates the elapsed counter on in-flight remotes; only
    // runs while at least one remote is actively fetching.
    fetchTicker->setInterval(1000);
    connect(fetchTicker, &QTimer::timeout, this, &FetchDeeznutzWindow::updateFetchElapsed);

    // Start timer based on loaded settings
    if (autoFetchCheckBox->isChecked()) {
        fetchTimer->start(globalIntervalSpinBox->value() * 60000);
    }

    // Show the window on launch unless the user opted to start in the tray.
    if (!startMinimizedCheckBox->isChecked()) {
        showWindow();
    }

    // Fetch immediately on launch rather than waiting a full interval, so the
    // status is fresh as soon as the app comes up. Deferred to the event loop so
    // the worker thread and UI are fully ready first.
    if (autoFetchCheckBox->isChecked()) {
        QTimer::singleShot(0, this, &FetchDeeznutzWindow::fetchAll);
    }
}

FetchDeeznutzWindow::~FetchDeeznutzWindow()
{
    saveSettings(); // persist window geometry (and settings) on quit
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

    repositoryModel = new RepositoryTreeModel(&repositories, this);
    repositoryView = new QTreeView();
    repositoryView->setModel(repositoryModel);
    repositoryView->setSelectionMode(QAbstractItemView::SingleSelection);
    repositoryView->setRootIsDecorated(true);
    repositoryView->setAlternatingRowColors(true);
    repositoryView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(repositoryView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &FetchDeeznutzWindow::onRepositorySelectionChanged);
    connect(repositoryView, &QTreeView::customContextMenuRequested, this, &FetchDeeznutzWindow::showContextMenu);
    connect(repositoryView, &QTreeView::doubleClicked, this, &FetchDeeznutzWindow::onRepositoryItemDoubleClicked);
    repoLayout->addWidget(repositoryView);

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

    startMinimizedCheckBox = new QCheckBox("Start minimized to tray");
    startMinimizedCheckBox->setChecked(false);
    startMinimizedCheckBox->setToolTip("When enabled, the app launches straight to the system tray instead of showing the window.");
    connect(startMinimizedCheckBox, &QCheckBox::toggled, this, &FetchDeeznutzWindow::saveSettings);

    // Initially update the enabled state of interval controls (will be updated again in loadSettings)
    updateAutoFetchControls();

    fetchAllButton = new QPushButton("Fetch All Now");
    connect(fetchAllButton, &QPushButton::clicked, this, &FetchDeeznutzWindow::fetchAll);

    settingsLayout->addRow("Global Interval:", globalIntervalSpinBox);
    settingsLayout->addRow("Fetch Timeout:", fetchTimeoutSpinBox);
    settingsLayout->addRow("Connection Timeout:", connectionTimeoutSpinBox);
    settingsLayout->addRow("", autoFetchCheckBox);
    settingsLayout->addRow("", startMinimizedCheckBox);
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
    GitRepository* repo = repositoryForIndex(repositoryView->currentIndex());
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
    GitRepository* repo = repositoryForIndex(repositoryView->currentIndex());
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
    const QModelIndex currentIndex = repositoryView->currentIndex();
    if (!currentIndex.isValid()) return;

    // If it's a repository row, remove just that repository.
    if (repositoryModel->isRepository(currentIndex)) {
        removeRepository();
        return;
    }

    // It's a directory row, get the directory path.
    QString dirPath = repositoryModel->directoryPath(currentIndex);
    if (dirPath.isEmpty()) return;
    
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
    const QModelIndex index = repositoryView->indexAt(pos);
    if (!index.isValid()) return;

    // Make sure the right-clicked row is the current one so the remove actions
    // operate on it.
    repositoryView->setCurrentIndex(index);

    QList<QAction*> actions = contextMenu->actions();
    QAction* removeRepoAction = actions[0]; // "Remove Repository"
    QAction* removeDirAction = actions[1];  // "Remove Directory"

    if (repositoryModel->isRepository(index)) {
        removeRepoAction->setVisible(true);
        removeDirAction->setVisible(false);
    } else if (repositoryModel->isDirectory(index)) {
        removeRepoAction->setVisible(false);
        removeDirAction->setVisible(true);
    } else {
        // Remote row: offer repository removal for its parent repo.
        removeRepoAction->setVisible(true);
        removeDirAction->setVisible(false);
    }

    contextMenu->exec(repositoryView->viewport()->mapToGlobal(pos));
}

void FetchDeeznutzWindow::fetchSelected()
{
    if (!fetchWorker) {
        logMessage("Error: Fetch worker is not initialized");
        return;
    }
    
    GitRepository* repo = repositoryForIndex(repositoryView->currentIndex());
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
    const bool hasSelection = repositoryForIndex(repositoryView->currentIndex()) != nullptr;
    editButton->setEnabled(hasSelection);
    removeButton->setEnabled(hasSelection);
    fetchSelectedButton->setEnabled(hasSelection);
}

void FetchDeeznutzWindow::onRepositoryItemDoubleClicked(const QModelIndex& index)
{
    // Only act on repository rows (not directory or remote rows).
    if (!repositoryModel->isRepository(index)) {
        return;
    }
    GitRepository* repo = repositoryForIndex(index);
    if (!repo) return;

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

void FetchDeeznutzWindow::onBackgroundFetchStarted(const QString& repoName)
{
    logMessage(QString("Started fetching: %1").arg(repoName));
    
    // Find the repository and update its status
    for (GitRepository& repo : repositories) {
        if (repo.name == repoName) {
            repo.status = "Fetching...";
            repositoryModel->updateRepositoryStatus(repoName);
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

void FetchDeeznutzWindow::onRemoteStatusChanged(const QString& repoName, const QString& remoteName, const QString& status)
{
    for (GitRepository& repo : repositories) {
        if (repo.name != repoName) {
            continue;
        }
        for (GitRemote& remote : repo.remotes) {
            if (remote.name == remoteName) {
                remote.status = status;
                if (status == "Fetching...") {
                    remote.fetchStartMs = QDateTime::currentMSecsSinceEpoch();
                    if (!fetchTicker->isActive()) {
                        fetchTicker->start();
                    }
                } else if (status == "Success") {
                    remote.lastFetch = QDateTime::currentDateTime().toString(Qt::ISODate);
                }
                repositoryModel->updateRemoteCounts(repoName, remoteName);
                break;
            }
        }
        break;
    }
}

void FetchDeeznutzWindow::updateFetchElapsed()
{
    if (repositoryModel->refreshActiveRemotes() == 0) {
        fetchTicker->stop();
    }
}

void FetchDeeznutzWindow::onNewTagsFound(const QString& repoName, const QStringList& tags)
{
    const QString tagList = tags.join(", ");
    logMessage(QString("🏷 New tag%1 in %2: %3").arg(tags.size() > 1 ? "s" : "", repoName, tagList));

    if (trayIcon && QSystemTrayIcon::supportsMessages()) {
        const QString title = QString("New tag%1 in %2").arg(tags.size() > 1 ? "s" : "", repoName);
        // Long timeout hint so the notification stays around until dismissed
        // (the actual persistence is ultimately up to the platform's notifier).
        trayIcon->showMessage(title, tagList, QSystemTrayIcon::Information, 24 * 60 * 60 * 1000);
    }
}

void FetchDeeznutzWindow::onBackgroundFetchFinished(const QString& repoName, bool success, const QString& message)
{
    logMessage(QString("%1 %2: %3").arg(success ? "✓" : "✗", repoName, message));
    
    // Update the in-memory status / last-fetch time. Neither is persisted, so
    // there is no need to rewrite the config on fetch completion.
    for (GitRepository& repo : repositories) {
        if (repo.name == repoName) {
            repo.status = success ? "Success" : "Error";
            if (success) {
                repo.lastFetch = QDateTime::currentDateTime().toString(Qt::ISODate);
            }
            repositoryModel->updateRepositoryStatus(repoName);
            // The fetch just updated the remote-tracking refs, so the standing
            // ahead/behind counts are stale (and would also be stale after an
            // external rebase of the local branch). Recompute them now.
            calculateCommitCountsAsync(repo);
            break;
        }
    }
}

void FetchDeeznutzWindow::onBackgroundFetchError(const QString& repoName, const QString& errorMessage)
{
    logMessage(QString("✗ Error fetching %1: %2").arg(repoName, errorMessage));
    
    // In-memory status only; nothing persistable changed.
    for (GitRepository& repo : repositories) {
        if (repo.name == repoName) {
            repo.status = "Error";
            repositoryModel->updateRepositoryStatus(repoName);
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
                    repositoryModel->updateRemoteCounts(repoName, remoteName);
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
    // Persist the current window geometry before hiding to the tray.
    saveSettings();
    // Hide to system tray instead of closing
    hide();
    event->ignore();
}

void FetchDeeznutzWindow::updateRepositoryTree()
{
    // Remember the selected repository so we can restore it after the rebuild.
    QString selectedName, selectedPath;
    if (GitRepository* selected = repositoryForIndex(repositoryView->currentIndex())) {
        selectedName = selected->name;
        selectedPath = selected->localPath;
    }
    const int scrollValue = repositoryView->verticalScrollBar()->value();

    repositoryModel->rebuild();
    repositoryView->expandAll();

    if (!selectedName.isEmpty()) {
        const QModelIndex restored = repositoryModel->indexForRepository(selectedName, selectedPath);
        if (restored.isValid()) {
            repositoryView->setCurrentIndex(restored);
        }
    }
    repositoryView->verticalScrollBar()->setValue(scrollValue);
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

GitRepository* FetchDeeznutzWindow::repositoryForIndex(const QModelIndex& index)
{
    const int repoIndex = repositoryModel->repositoryIndex(index);
    if (repoIndex < 0 || repoIndex >= repositories.size()) {
        return nullptr;
    }
    return &repositories[repoIndex];
}

void FetchDeeznutzWindow::logMessage(const QString& message)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    logTextEdit->append(QString("[%1] %2").arg(timestamp, message));
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

    // Load start-minimized preference (default: false -> show the window on launch)
    startMinimizedCheckBox->setChecked(settings.value("startMinimized", false).toBool());

    // Restore the saved window geometry (size/position) if present.
    const QByteArray geometry = settings.value("windowGeometry").toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

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
    settings.setValue("startMinimized", startMinimizedCheckBox->isChecked());
    settings.setValue("windowGeometry", saveGeometry());
    
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
    RepositoryStore::LoadResult result = m_store.load();
    for (const QString& message : result.messages) {
        logMessage(message);
    }

    repositories = result.repositories;

    // Calculate commit counts for loaded repositories asynchronously
    for (const GitRepository& repo : repositories) {
        calculateCommitCountsAsync(repo);
    }
}

void FetchDeeznutzWindow::saveRepositories()
{
    QString error;
    if (m_store.save(repositories, &error)) {
        logMessage("Configuration saved");
    } else {
        logMessage(QString("Failed to save configuration: %1").arg(error));
    }
}

