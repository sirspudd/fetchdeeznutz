#include "gitfetchworker.h"
#include "gitutils.h"
#include <git2.h>
#include <QDir>
#include <QFile>
#include <QtConcurrent>
#include <QFuture>
#include <QMutexLocker>
#include <QDebug>
#include <chrono>

namespace {
// Payload handed to libgit2 fetch callbacks. Lets the callbacks reach the
// worker (for credentials) and enforce an overall fetch deadline / cancellation.
struct FetchCallbackPayload {
    GitFetchWorker* worker;
    const std::atomic<bool>* stopRequested;
    std::chrono::steady_clock::time_point deadline;

    bool shouldAbort() const {
        return stopRequested->load() || std::chrono::steady_clock::now() >= deadline;
    }
};

int abortIfNeeded(void* payload) {
    auto* p = static_cast<FetchCallbackPayload*>(payload);
    return (p && p->shouldAbort()) ? GIT_EUSER : 0;
}
} // namespace

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

    // Apply the connection / server-side timeouts. These are libgit2 global
    // options; serialize the write under the git mutex to avoid racing with
    // other threads reading them mid-connect.
    const int connectTimeoutMs = m_connectionTimeoutSeconds.load() * 1000;
    {
        QMutexLocker locker(&g_gitMutex);
        git_libgit2_opts(GIT_OPT_SET_SERVER_CONNECT_TIMEOUT, connectTimeoutMs);
        git_libgit2_opts(GIT_OPT_SET_SERVER_TIMEOUT, connectTimeoutMs);
    }

    // Overall deadline for the whole repository fetch.
    FetchCallbackPayload payload;
    payload.worker = this;
    payload.stopRequested = &m_stopRequested;
    payload.deadline = std::chrono::steady_clock::now() +
                       std::chrono::seconds(m_timeoutSeconds.load());

    bool allSuccessful = true;
    QStringList failedRemotes;
    int totalRemotes = repo.remotes.size();
    int completedRemotes = 0;

    // Fetch from all remotes
    for (const GitRemote& remote : repo.remotes) {
        if (payload.shouldAbort()) {
            {
                QMutexLocker locker(&g_gitMutex);
                git_repository_free(repository);
            }
            const bool deadlineHit = std::chrono::steady_clock::now() >= payload.deadline;
            emit fetchFinished(repo.name, false,
                               deadlineHit
                                   ? QString("Fetch timed out after %1 seconds").arg(m_timeoutSeconds.load())
                                   : QString("Fetch cancelled"));
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
            auto* cb = static_cast<FetchCallbackPayload*>(payload);
            return cb->worker->sshKeyCallback(out, url, username_from_url, allowed_types, cb->worker);
        };
        // Abort the transfer if the user stops or the overall deadline passes.
        fetch_opts.callbacks.transfer_progress = [](const git_indexer_progress*, void *payload) -> int {
            return abortIfNeeded(payload);
        };
        fetch_opts.callbacks.sideband_progress = [](const char*, int, void *payload) -> int {
            return abortIfNeeded(payload);
        };
        fetch_opts.callbacks.payload = &payload;

        // Don't automatically download tags - let user control this
        fetch_opts.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_NONE;

        error = GitUtils::fetchRemoteWithTimeout(git_remote, fetch_opts, m_connectionTimeoutSeconds.load());

        if (error < 0) {
            if (std::chrono::steady_clock::now() >= payload.deadline) {
                failedRemotes.append(remote.name + " (timed out)");
            } else if (error == GIT_EUSER || m_stopRequested.load()) {
                failedRemotes.append(remote.name + " (cancelled)");
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

    if (allSuccessful) {
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
