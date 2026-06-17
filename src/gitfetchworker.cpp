#include "gitfetchworker.h"
#include "gitutils.h"
#include <git2.h>
#include <QDir>
#include <QFile>
#include <QtConcurrent>
#include <QFuture>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QThread>
#include <QDebug>
#include <chrono>
#include <memory>
#include <utility>

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

// Shared aggregation state for the remotes of a single repository fetch. The
// per-remote pool tasks and the watchdog all hold a shared_ptr to it; whichever
// completes the set (or times out) first finalizes and emits fetchFinished.
struct RepoFetchState {
    QMutex mutex;
    QString repoName;
    QStringList remoteNames;
    QSet<QString> completed;
    QStringList failed;
    bool allSuccessful = true;
    bool finished = false;
    int timeoutSeconds = 0;
};
} // namespace

GitFetchWorker::GitFetchWorker(QObject *parent)
    : QObject(parent)
    , m_stopRequested(false)
    , m_timeoutSeconds(300) // Default 5 minutes
    , m_connectionTimeoutSeconds(5) // Default 5 seconds
{
    // Bound concurrency so a burst of repositories/remotes doesn't spawn an
    // unbounded number of network threads, while still letting independent
    // remotes fetch in parallel.
    m_pool.setMaxThreadCount(qMax(4, QThread::idealThreadCount() * 2));
}

GitFetchWorker::~GitFetchWorker()
{
}

void GitFetchWorker::fetchRepository(const GitRepository& repo)
{
    m_stopRequested = false;
    emit fetchStarted(repo.name);

    if (repo.remotes.isEmpty()) {
        emit fetchError(repo.name, "No remotes configured");
        return;
    }

    if (!GitUtils::isRepositoryValid(repo.localPath)) {
        emit fetchError(repo.name, QString("Repository not found at: %1").arg(repo.localPath));
        return;
    }

    // Apply the connection / server-side timeouts. These are libgit2 global
    // options; serialize the write under the git mutex to avoid racing with
    // other threads reading them mid-connect.
    const int timeoutSeconds = m_timeoutSeconds.load();
    {
        QMutexLocker locker(&g_gitMutex);
        const int connectTimeoutMs = m_connectionTimeoutSeconds.load() * 1000;
        git_libgit2_opts(GIT_OPT_SET_SERVER_CONNECT_TIMEOUT, connectTimeoutMs);
        git_libgit2_opts(GIT_OPT_SET_SERVER_TIMEOUT, connectTimeoutMs);
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);

    auto state = std::make_shared<RepoFetchState>();
    state->repoName = repo.name;
    state->timeoutSeconds = timeoutSeconds;
    for (const GitRemote& remote : repo.remotes) {
        state->remoteNames.append(remote.name);
        emit remoteStatusChanged(repo.name, remote.name, QStringLiteral("Queued"));
    }

    // Dispatch each remote onto the bounded pool with its own handle so one
    // slow/hung remote cannot block its siblings.
    for (const GitRemote& remote : repo.remotes) {
        const QString repoName = repo.name;
        const QString repoPath = repo.localPath;
        const GitRemote r = remote;
        [[maybe_unused]] QFuture<void> f = QtConcurrent::run(&m_pool, [this, repoName, repoPath, r, deadline, state]() {
            {
                QMutexLocker lk(&state->mutex);
                if (state->finished) {
                    return; // repository fetch already finalized (e.g. timed out)
                }
            }

            QString statusLabel;
            const bool ok = fetchOneRemote(repoName, repoPath, r, deadline, statusLabel);

            QMutexLocker lk(&state->mutex);
            if (state->finished || state->completed.contains(r.name)) {
                return;
            }
            state->completed.insert(r.name);
            emit remoteStatusChanged(repoName, r.name, statusLabel);
            if (!ok) {
                state->allSuccessful = false;
                state->failed.append(r.name);
            }
            if (state->completed.size() == state->remoteNames.size()) {
                state->finished = true;
                if (state->allSuccessful) {
                    emit fetchFinished(repoName, true, "All remotes fetched successfully");
                } else {
                    emit fetchFinished(repoName, false, QString("Some remotes failed: %1").arg(state->failed.join(", ")));
                }
            }
        });
    }

    // Watchdog: even if a remote hangs in a phase libgit2 can't interrupt (e.g.
    // an SSH connect/auth stall, where the transfer callbacks never fire), the
    // UI must not wait forever. This fires on the worker thread's event loop and
    // finalizes the repository, marking any still-pending remotes as timed out.
    // The orphaned fetch keeps running until libgit2 returns, then sees
    // `finished` and quietly discards its result.
    QTimer::singleShot(timeoutSeconds * 1000, this, [this, state]() {
        QMutexLocker lk(&state->mutex);
        if (state->finished) {
            return;
        }
        state->finished = true;
        for (const QString& name : std::as_const(state->remoteNames)) {
            if (!state->completed.contains(name)) {
                emit remoteStatusChanged(state->repoName, name, QStringLiteral("Timeout"));
                state->failed.append(name + " (timed out)");
                state->allSuccessful = false;
            }
        }
        emit fetchFinished(state->repoName, false,
                           QString("Fetch timed out after %1 seconds; %2 remote(s) still running in the background")
                               .arg(state->timeoutSeconds)
                               .arg(state->remoteNames.size() - state->completed.size()));
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

bool GitFetchWorker::fetchOneRemote(const QString& repoName, const QString& repoPath, const GitRemote& remote,
                                    std::chrono::steady_clock::time_point deadline, QString& statusLabel)
{
    emit remoteStatusChanged(repoName, remote.name, QStringLiteral("Fetching..."));

    // Each task uses its own repository handle: libgit2 handles must not be
    // shared across threads.
    git_repository *repository = nullptr;
    int error;
    {
        QMutexLocker locker(&g_gitMutex);
        error = git_repository_open(&repository, repoPath.toLocal8Bit().constData());
    }
    if (error < 0) {
        statusLabel = QStringLiteral("Error");
        return false;
    }

    git_remote *git_remote = nullptr;
    {
        QMutexLocker locker(&g_gitMutex);
        error = git_remote_lookup(&git_remote, repository, remote.name.toLocal8Bit().constData());
        if (error < 0) {
            // If the remote doesn't exist yet, create it.
            error = git_remote_create(&git_remote, repository, remote.name.toLocal8Bit().constData(), remote.url.toLocal8Bit().constData());
        }
    }
    if (error < 0) {
        {
            QMutexLocker locker(&g_gitMutex);
            git_repository_free(repository);
        }
        statusLabel = QStringLiteral("Error");
        return false;
    }

    FetchCallbackPayload payload;
    payload.worker = this;
    payload.stopRequested = &m_stopRequested;
    payload.deadline = deadline;

    git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
    fetch_opts.callbacks.credentials = [](git_credential **out, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload) -> int {
        auto* cb = static_cast<FetchCallbackPayload*>(payload);
        return cb->worker->sshKeyCallback(out, url, username_from_url, allowed_types, cb->worker);
    };
    fetch_opts.callbacks.transfer_progress = [](const git_indexer_progress*, void *payload) -> int {
        return abortIfNeeded(payload);
    };
    fetch_opts.callbacks.sideband_progress = [](const char*, int, void *payload) -> int {
        return abortIfNeeded(payload);
    };
    fetch_opts.callbacks.payload = &payload;
    fetch_opts.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_NONE;

    error = GitUtils::fetchRemoteWithTimeout(git_remote, fetch_opts, m_connectionTimeoutSeconds.load());

    bool success = false;
    if (error < 0) {
        if (std::chrono::steady_clock::now() >= deadline) {
            statusLabel = QStringLiteral("Timeout");
        } else if (error == GIT_EUSER || m_stopRequested.load()) {
            statusLabel = QStringLiteral("Cancelled");
        } else {
            statusLabel = QStringLiteral("Error");
        }
    } else {
        statusLabel = QStringLiteral("Success");
        success = true;
    }

    {
        QMutexLocker locker(&g_gitMutex);
        git_remote_free(git_remote);
        git_repository_free(repository);
    }
    return success;
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
