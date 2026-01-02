#include "gitfetchworker.h"
#include "gitutils.h"
#include <git2.h>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QtConcurrent>
#include <QFuture>
#include <QMutexLocker>
#include <QDebug>

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
    
    // Run fetch in parallel using QtConcurrent, but all git operations will be serialized by g_gitMutex
    // Store the future to avoid the nodiscard warning, but we don't actively track it
    [[maybe_unused]] QFuture<void> future = QtConcurrent::run([this, repo]() {
        performFetch(repo);
    });
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
    if (!GitUtils::isRepositoryValid(repo.localPath)) {
        emit fetchError(repo.name, QString("Repository not found at: %1").arg(repo.localPath));
        return;
    }

    // Open the repository - protected by mutex
    git_repository *repository = nullptr;
    int error;
    {
        QMutexLocker locker(&g_gitMutex);
        error = git_repository_open(&repository, repo.localPath.toLocal8Bit().constData());
    }
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
            {
                QMutexLocker locker(&g_gitMutex);
                git_repository_free(repository);
            }
            if (timeoutOccurred) {
                emit fetchFinished(repo.name, false, QString("Fetch timed out after %1 seconds").arg(m_timeoutSeconds));
            } else {
                emit fetchFinished(repo.name, false, "Fetch cancelled");
            }
            return;
        }

        emit fetchProgress(repo.name, remote.name, (completedRemotes * 100) / totalRemotes);

        git_remote *git_remote = nullptr;
        {
            QMutexLocker locker(&g_gitMutex);
            error = git_remote_lookup(&git_remote, repository, remote.name.toLocal8Bit().constData());
            if (error < 0) {
                // If remote doesn't exist, add it
                error = git_remote_create(&git_remote, repository, remote.name.toLocal8Bit().constData(), remote.url.toLocal8Bit().constData());
            }
        }
        if (error < 0) {
            failedRemotes.append(remote.name);
            allSuccessful = false;
            completedRemotes++;
            continue;
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
        error = GitUtils::fetchRemoteWithTimeout(git_remote, fetch_opts, m_connectionTimeoutSeconds);

        if (error < 0) {
            if (error == GIT_ETIMEOUT) {
                // Connection timeout - this is a custom error code we return
                failedRemotes.append(remote.name + " (connection timeout)");
            } else {
                failedRemotes.append(remote.name);
            }
            allSuccessful = false;
        }

        {
            QMutexLocker locker(&g_gitMutex);
            git_remote_free(git_remote);
        }
        completedRemotes++;
    }

    {
        QMutexLocker locker(&g_gitMutex);
        git_repository_free(repository);
    }
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
    return GitUtils::getGitErrorMessage(error);
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

void GitFetchWorker::calculateRemoteCommitCounts(git_repository* repository, GitRemote& remote, const QString& branch, const QString& repoName)
{
    GitUtils::calculateRemoteCommitCounts(repository, remote, branch, repoName);
    emit commitCountsUpdated(repoName, remote.name, remote.commitsAhead, remote.commitsBehind);
}

bool GitFetchWorker::isRepositoryValid(const QString& path)
{
    return GitUtils::isRepositoryValid(path);
}

int GitFetchWorker::fetchRemoteWithTimeout(git_remote* git_remote, const git_fetch_options& fetch_opts, int timeoutSeconds)
{
    return GitUtils::fetchRemoteWithTimeout(git_remote, fetch_opts, timeoutSeconds);
}
