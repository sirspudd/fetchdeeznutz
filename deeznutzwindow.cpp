#include "deeznutzwindow.h"

RepositoryDialog::RepositoryDialog(const GitRepository& repo, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(repo.name.isEmpty() ? "Add Repository" : "Edit Repository");
    setModal(true);
    resize(400, 300);
    
    QFormLayout *layout = new QFormLayout(this);
    
    nameEdit = new QLineEdit(repo.name);
    urlEdit = new QLineEdit(repo.url);
    pathEdit = new QLineEdit(repo.localPath);
    branchEdit = new QLineEdit(repo.branch.isEmpty() ? "main" : repo.branch);
    intervalSpinBox = new QSpinBox();
    intervalSpinBox->setRange(1, 1440); // 1 minute to 24 hours
    intervalSpinBox->setValue(repo.fetchInterval);
    intervalSpinBox->setSuffix(" minutes");
    enabledCheckBox = new QCheckBox();
    enabledCheckBox->setChecked(repo.enabled);
    
    layout->addRow("Name:", nameEdit);
    layout->addRow("URL:", urlEdit);
    layout->addRow("Local Path:", pathEdit);
    layout->addRow("Branch:", branchEdit);
    layout->addRow("Fetch Interval:", intervalSpinBox);
    layout->addRow("Enabled:", enabledCheckBox);
    
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    layout->addRow(buttonBox);
}

GitRepository RepositoryDialog::getRepository() const
{
    GitRepository repo;
    repo.name = nameEdit->text().trimmed();
    repo.url = urlEdit->text().trimmed();
    repo.localPath = pathEdit->text().trimmed();
    repo.branch = branchEdit->text().trimmed();
    repo.fetchInterval = intervalSpinBox->value();
    repo.enabled = enabledCheckBox->isChecked();
    return repo;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , fetchTimer(new QTimer(this))
    , currentFetchIndex(-1)
    , isFetching(false)
{
    setWindowTitle("Git Repository Fetcher");
    setMinimumSize(800, 600);
    
    setupUI();
    loadRepositories();
    updateRepositoryList();
    
    // Connect timer for scheduled fetching
    connect(fetchTimer, &QTimer::timeout, this, &MainWindow::performScheduledFetch);
    
    // Initialize libgit2
    git_libgit2_init();
    
    // Start with a 1-minute timer
    fetchTimer->start(60000); // 60 seconds
}

MainWindow::~MainWindow()
{
    saveRepositories();
    git_libgit2_shutdown();
}

void MainWindow::setupUI()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    
    // Left panel - Repository list and controls
    QVBoxLayout *leftLayout = new QVBoxLayout();
    
    // Repository list
    QGroupBox *repoGroup = new QGroupBox("Repositories");
    QVBoxLayout *repoLayout = new QVBoxLayout(repoGroup);
    
    repositoryList = new QListWidget();
    repositoryList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(repositoryList, &QListWidget::itemSelectionChanged, this, &MainWindow::onRepositorySelectionChanged);
    repoLayout->addWidget(repositoryList);
    
    // Repository control buttons
    QHBoxLayout *repoButtonLayout = new QHBoxLayout();
    addButton = new QPushButton("Add Repo");
    addDirectoryButton = new QPushButton("Add Directory");
    editButton = new QPushButton("Edit");
    removeButton = new QPushButton("Remove");
    fetchSelectedButton = new QPushButton("Fetch Selected");
    
    connect(addButton, &QPushButton::clicked, this, &MainWindow::addRepository);
    connect(addDirectoryButton, &QPushButton::clicked, this, &MainWindow::addDirectory);
    connect(editButton, &QPushButton::clicked, this, &MainWindow::editRepository);
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::removeRepository);
    connect(fetchSelectedButton, &QPushButton::clicked, this, &MainWindow::fetchSelected);
    
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
    connect(globalIntervalSpinBox, &QSpinBox::valueChanged, this, &MainWindow::onFetchIntervalChanged);
    
    autoFetchCheckBox = new QCheckBox("Enable Auto Fetch");
    autoFetchCheckBox->setChecked(true);
    connect(autoFetchCheckBox, &QCheckBox::toggled, this, &MainWindow::onAutoFetchToggled);
    
    fetchAllButton = new QPushButton("Fetch All Now");
    connect(fetchAllButton, &QPushButton::clicked, this, &MainWindow::fetchAll);
    
    settingsLayout->addRow("Global Interval:", globalIntervalSpinBox);
    settingsLayout->addRow("", autoFetchCheckBox);
    settingsLayout->addRow("", fetchAllButton);
    
    leftLayout->addWidget(settingsGroup);
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

void MainWindow::addRepository()
{
    RepositoryDialog dialog(GitRepository(), this);
    if (dialog.exec() == QDialog::Accepted) {
        GitRepository repo = dialog.getRepository();
        if (!repo.name.isEmpty() && !repo.url.isEmpty()) {
            repositories.append(repo);
            updateRepositoryList();
            saveRepositories();
            logMessage(QString("Added repository: %1").arg(repo.name));
        } else {
            QMessageBox::warning(this, "Invalid Repository", "Name and URL are required.");
        }
    }
}

void MainWindow::addDirectory()
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

void MainWindow::editRepository()
{
    int currentRow = repositoryList->currentRow();
    if (currentRow >= 0 && currentRow < repositories.size()) {
        RepositoryDialog dialog(repositories[currentRow], this);
        if (dialog.exec() == QDialog::Accepted) {
            GitRepository repo = dialog.getRepository();
            if (!repo.name.isEmpty() && !repo.url.isEmpty()) {
                repositories[currentRow] = repo;
                updateRepositoryList();
                saveRepositories();
                logMessage(QString("Updated repository: %1").arg(repo.name));
            } else {
                QMessageBox::warning(this, "Invalid Repository", "Name and URL are required.");
            }
        }
    }
}

void MainWindow::removeRepository()
{
    int currentRow = repositoryList->currentRow();
    if (currentRow >= 0 && currentRow < repositories.size()) {
        QString repoName = repositories[currentRow].name;
        int ret = QMessageBox::question(this, "Remove Repository", 
                                       QString("Are you sure you want to remove '%1'?").arg(repoName),
                                       QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            repositories.removeAt(currentRow);
            updateRepositoryList();
            saveRepositories();
            logMessage(QString("Removed repository: %1").arg(repoName));
        }
    }
}

void MainWindow::fetchSelected()
{
    int currentRow = repositoryList->currentRow();
    if (currentRow >= 0 && currentRow < repositories.size()) {
        fetchRepository(repositories[currentRow]);
    }
}

void MainWindow::fetchAll()
{
    logMessage("Starting fetch for all enabled repositories...");
    for (GitRepository& repo : repositories) {
        if (repo.enabled) {
            fetchRepository(repo);
        }
    }
}

void MainWindow::onRepositorySelectionChanged()
{
    bool hasSelection = repositoryList->currentRow() >= 0;
    editButton->setEnabled(hasSelection);
    removeButton->setEnabled(hasSelection);
    fetchSelectedButton->setEnabled(hasSelection);
}

void MainWindow::onFetchIntervalChanged()
{
    if (autoFetchCheckBox->isChecked()) {
        fetchTimer->setInterval(globalIntervalSpinBox->value() * 60000);
        logMessage(QString("Auto-fetch interval changed to %1 minutes").arg(globalIntervalSpinBox->value()));
    }
}

void MainWindow::onAutoFetchToggled()
{
    if (autoFetchCheckBox->isChecked()) {
        startScheduledFetch();
        logMessage("Auto-fetch enabled");
    } else {
        stopScheduledFetch();
        logMessage("Auto-fetch disabled");
    }
}

void MainWindow::performScheduledFetch()
{
    if (!autoFetchCheckBox->isChecked()) return;
    
    QDateTime now = QDateTime::currentDateTime();
    for (GitRepository& repo : repositories) {
        if (!repo.enabled) continue;
        
        QDateTime lastFetch = QDateTime::fromString(repo.lastFetch, Qt::ISODate);
        if (!lastFetch.isValid() || lastFetch.addSecs(repo.fetchInterval * 60) <= now) {
            fetchRepository(repo);
        }
    }
}

void MainWindow::onFetchFinished()
{
    if (currentFetchIndex >= 0 && currentFetchIndex < repositories.size()) {
        GitRepository& repo = repositories[currentFetchIndex];
        repo.status = "Success";
        repo.lastFetch = QDateTime::currentDateTime().toString(Qt::ISODate);
        logMessage(QString("✓ Successfully fetched: %1").arg(repo.name));
        
        updateRepositoryList();
        saveRepositories();
        currentFetchIndex = -1;
    }
    
    isFetching = false;
}

void MainWindow::onFetchError(const QString& errorMessage)
{
    if (currentFetchIndex >= 0 && currentFetchIndex < repositories.size()) {
        GitRepository& repo = repositories[currentFetchIndex];
        repo.status = "Error";
        logMessage(QString("✗ Error fetching: %1 - %2").arg(repo.name, errorMessage));
        updateRepositoryList();
        saveRepositories();
        currentFetchIndex = -1;
    }
    
    isFetching = false;
}

void MainWindow::updateRepositoryList()
{
    repositoryList->clear();
    for (const GitRepository& repo : repositories) {
        QString statusIcon = repo.enabled ? "●" : "○";
        QString statusText = repo.status.isEmpty() ? "Ready" : repo.status;
        QString itemText = QString("%1 %2 - %3 (%4)")
                          .arg(statusIcon)
                          .arg(repo.name)
                          .arg(statusText)
                          .arg(repo.branch);
        repositoryList->addItem(itemText);
    }
}

void MainWindow::startScheduledFetch()
{
    fetchTimer->setInterval(globalIntervalSpinBox->value() * 60000);
    fetchTimer->start();
}

void MainWindow::stopScheduledFetch()
{
    fetchTimer->stop();
}

void MainWindow::fetchRepository(GitRepository& repo)
{
    if (isFetching) {
        logMessage("Another fetch operation is already in progress");
        return;
    }
    
    logMessage(QString("Starting fetch for: %1").arg(repo.name));
    repo.status = "Fetching...";
    updateRepositoryList();
    
    currentFetchIndex = repositories.indexOf(repo);
    isFetching = true;
    
    // Ensure the directory exists
    QDir().mkpath(repo.localPath);
    
    // Check if repository exists, if not clone it
    if (!isRepositoryValid(repo.localPath)) {
        logMessage(QString("Repository not found, cloning: %1").arg(repo.name));
        if (!cloneRepository(repo)) {
            onFetchError("Failed to clone repository");
            return;
        }
    }
    
    // Open the repository
    git_repository *repository = nullptr;
    int error = git_repository_open(&repository, repo.localPath.toLocal8Bit().constData());
    if (error < 0) {
        onFetchError(getGitErrorMessage(error));
        return;
    }
    
    // Get the remote
    git_remote *remote = nullptr;
    error = git_remote_lookup(&remote, repository, "origin");
    if (error < 0) {
        // If origin doesn't exist, add it
        error = git_remote_create(&remote, repository, "origin", repo.url.toLocal8Bit().constData());
        if (error < 0) {
            git_repository_free(repository);
            onFetchError(getGitErrorMessage(error));
            return;
        }
    }
    
    // Fetch from remote
    git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
    error = git_remote_fetch(remote, nullptr, &fetch_opts, nullptr);
    
    git_remote_free(remote);
    git_repository_free(repository);
    
    if (error < 0) {
        onFetchError(getGitErrorMessage(error));
    } else {
        onFetchFinished();
    }
}

bool MainWindow::cloneRepository(const GitRepository& repo)
{
    git_repository *repository = nullptr;
    git_clone_options clone_opts = GIT_CLONE_OPTIONS_INIT;
    
    // Set checkout options
    clone_opts.checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;
    
    int error = git_clone(&repository, repo.url.toLocal8Bit().constData(), 
                         repo.localPath.toLocal8Bit().constData(), &clone_opts);
    
    if (error < 0) {
        logMessage(QString("Failed to clone repository: %1").arg(getGitErrorMessage(error)));
        return false;
    }
    
    git_repository_free(repository);
    logMessage(QString("Successfully cloned repository: %1").arg(repo.name));
    return true;
}

bool MainWindow::isRepositoryValid(const QString& path)
{
    git_repository *repository = nullptr;
    int error = git_repository_open(&repository, path.toLocal8Bit().constData());
    
    if (error < 0) {
        return false;
    }
    
    git_repository_free(repository);
    return true;
}

QString MainWindow::getGitErrorMessage(int error) const
{
    const git_error *git_error = git_error_last();
    if (git_error && git_error->message) {
        return QString::fromUtf8(git_error->message);
    }
    return QString("Unknown Git error: %1").arg(error);
}

void MainWindow::logMessage(const QString& message)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    logTextEdit->append(QString("[%1] %2").arg(timestamp, message));
}

QString MainWindow::getConfigFilePath() const
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    return QDir(configDir).filePath("repositories.json");
}

void MainWindow::loadRepositories()
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
}

void MainWindow::saveRepositories()
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

void MainWindow::scanDirectoryForRepositories(const QString& directoryPath)
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
        repo.url = getRepositoryUrl(repoPath);
        repo.localPath = repoPath;
        repo.branch = getRepositoryBranch(repoPath);
        repo.fetchInterval = 60; // Default 1 hour
        repo.enabled = true;
        repo.status = "Ready";
        
        if (!repo.name.isEmpty() && !repo.url.isEmpty()) {
            repositories.append(repo);
            addedCount++;
            logMessage(QString("Discovered repository: %1 at %2").arg(repo.name, repoPath));
        } else {
            logMessage(QString("Skipped invalid repository at: %1").arg(repoPath));
        }
    }
    
    if (addedCount > 0) {
        updateRepositoryList();
        saveRepositories();
    }
    
    logMessage(QString("Directory scan complete: %1 repositories added, %2 skipped (already exist)").arg(addedCount).arg(skippedCount));
}

QStringList MainWindow::findGitRepositories(const QString& directoryPath, const QStringList& excludeDirs)
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

bool MainWindow::isGitRepository(const QString& path)
{
    QDir dir(path);
    return dir.exists(".git");
}

QString MainWindow::getRepositoryName(const QString& path)
{
    QDir dir(path);
    return dir.dirName();
}

QString MainWindow::getRepositoryUrl(const QString& path)
{
    git_repository *repository = nullptr;
    int error = git_repository_open(&repository, path.toLocal8Bit().constData());
    
    if (error < 0) {
        return QString();
    }
    
    git_remote *remote = nullptr;
    error = git_remote_lookup(&remote, repository, "origin");
    
    QString url;
    if (error >= 0) {
        const char *remote_url = git_remote_url(remote);
        if (remote_url) {
            url = QString::fromUtf8(remote_url);
        }
        git_remote_free(remote);
    }
    
    git_repository_free(repository);
    return url;
}

QString MainWindow::getRepositoryBranch(const QString& path)
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

