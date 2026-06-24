#include "gitfetchworker.h"
#include "gitutils.h"
#include <QtConcurrent>
#include <QFuture>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QThread>
#include <QDebug>
#include <chrono>
#include <memory>

namespace {
// Shared aggregation state for the remotes of a single repository fetch. The
// per-remote pool tasks and the watchdog all hold a shared_ptr to it; whichever
// completes the set (or times out) first finalizes and emits fetchFinished.
struct RepoFetchState {
    QMutex mutex;
    QString repoName;
    QString repoPath;
    QStringList remoteNames;
    QStringList tagsBefore; // tag snapshot taken before any remote fetched
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
    // unbounded number of network processes, while still letting independent
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

    const int timeoutSeconds = m_timeoutSeconds.load();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);

    auto state = std::make_shared<RepoFetchState>();
    state->repoName = repo.name;
    state->repoPath = repo.localPath;
    state->timeoutSeconds = timeoutSeconds;
    // Snapshot tags up front so we can report any that the fetch brings in.
    state->tagsBefore = GitUtils::listTags(repo.localPath);
    for (const GitRemote& remote : repo.remotes) {
        state->remoteNames.append(remote.name);
        emit remoteStatusChanged(repo.name, remote.name, QStringLiteral("Queued"));
    }

    // Dispatch each remote onto the bounded pool with its own git process so one
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

            bool doFinalize = false;
            bool finishSuccess = false;
            QString finishMessage;
            {
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
                    doFinalize = true;
                    finishSuccess = state->allSuccessful;
                    finishMessage = state->allSuccessful
                                        ? QStringLiteral("All remotes fetched successfully")
                                        : QString("Some remotes failed: %1").arg(state->failed.join(", "));
                }
            }
            // Tag diff + fetchFinished are done outside the lock: they touch git
            // (I/O) and only run once finalized, when no other task will mutate state.
            if (doFinalize) {
                emit fetchFinished(repoName, finishSuccess, finishMessage);
                checkForNewTags(repoName, repoPath, state->tagsBefore);
            }
        });
    }

    // Watchdog backstop: each remote process already self-terminates at the
    // deadline / on a stall, but this guarantees the UI is finalized even in the
    // unlikely event a process is slow to die. It marks any still-pending remotes
    // as timed out and finalizes the repository.
    QTimer::singleShot(timeoutSeconds * 1000, this, [this, state]() {
        QString message;
        {
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
            message = QString("Fetch timed out after %1 seconds").arg(state->timeoutSeconds);
        }
        emit fetchFinished(state->repoName, false, message);
        // Remotes that completed before the deadline may still have delivered tags.
        checkForNewTags(state->repoName, state->repoPath, state->tagsBefore);
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

    const int connectSeconds = qMax(1, m_connectionTimeoutSeconds.load());

    QProcess proc;
    // Merge stderr (where git writes --progress sideband) into stdout so we can
    // drain a single channel. We do NOT use output for stall detection anymore:
    // doing so would kill an in-progress passphrase prompt (which is silent).
    proc.setProcessChannelMode(QProcess::MergedChannels);

    // Base = the user's resolved login/interactive shell environment (so SSH
    // agent, askpass and PATH match a terminal they opened), with
    // GIT_TERMINAL_PROMPT=0 already layered on. We deliberately do NOT set
    // BatchMode: ssh must be allowed to prompt for a locked key's passphrase via
    // whatever agent/askpass the user's session provides -- this is
    // desktop-environment agnostic; we make no assumption about KDE/GNOME/etc.
    // and simply don't suppress the mechanism the user already has configured.
    QProcessEnvironment env = GitUtils::baseGitEnvironment();
    QString sshCmd = env.value(QStringLiteral("GIT_SSH_COMMAND"));
    if (sshCmd.isEmpty()) {
        sshCmd = QStringLiteral("ssh");
    }
    // Stall handling happens at the transport layer instead of by watching
    // output, so it never interferes with an interactive passphrase prompt:
    //  - ConnectTimeout bounds the initial TCP connect (dead host fails fast);
    //  - ServerAlive* makes ssh detect a stalled/dead connection AFTER it's
    //    established (these keepalives only run post-auth, so they don't race a
    //    human typing a passphrase) and exit on its own.
    sshCmd += QStringLiteral(" -o ConnectTimeout=%1 -o ServerAliveInterval=%1 -o ServerAliveCountMax=3").arg(connectSeconds);
    env.insert(QStringLiteral("GIT_SSH_COMMAND"), sshCmd);
    proc.setProcessEnvironment(env);

    // HTTP(S) analog of ssh keepalive death-detection: abort if throughput stays
    // effectively dead (< 1 byte/s) for a sustained window.
    const int httpStallSeconds = qMax(connectSeconds * 3, 30);
    const QStringList args = {QStringLiteral("-C"), repoPath,
                              QStringLiteral("-c"), QStringLiteral("http.lowSpeedLimit=1"),
                              QStringLiteral("-c"), QStringLiteral("http.lowSpeedTime=%1").arg(httpStallSeconds),
                              QStringLiteral("fetch"), QStringLiteral("--progress"),
                              remote.name};
    proc.start(QStringLiteral("git"), args);
    if (!proc.waitForStarted(5000)) {
        statusLabel = QStringLiteral("Error");
        return false;
    }

    // The overall deadline is the hard backstop (it also bounds how long a
    // passphrase prompt may sit). Mid-flight network stalls are handled by the
    // ssh/http options above, so we don't need a no-output timer here -- we just
    // drain output so a chatty fetch can't block on a full pipe, and poll for
    // cancellation / the deadline.
    enum class AbortReason { None, Stop, Deadline } reason = AbortReason::None;

    for (;;) {
        if (proc.waitForReadyRead(200)) {
            proc.readAll(); // drain; don't let a full pipe block git
        }

        if (proc.state() == QProcess::NotRunning) {
            proc.readAll(); // drain any trailing output
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        if (m_stopRequested.load()) { reason = AbortReason::Stop; break; }
        if (now >= deadline) { reason = AbortReason::Deadline; break; }
    }

    if (reason != AbortReason::None) {
        proc.kill();
        proc.waitForFinished(2000);
        statusLabel = (reason == AbortReason::Stop) ? QStringLiteral("Cancelled")
                                                    : QStringLiteral("Timeout");
        return false;
    }

    proc.waitForFinished(2000);
    const bool success = (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0);
    statusLabel = success ? QStringLiteral("Success") : QStringLiteral("Error");
    return success;
}

void GitFetchWorker::checkForNewTags(const QString& repoName, const QString& repoPath, const QStringList& tagsBefore)
{
    const QStringList tagsAfter = GitUtils::listTags(repoPath);
    if (tagsAfter.isEmpty()) {
        return;
    }
    const QSet<QString> before(tagsBefore.begin(), tagsBefore.end());
    QStringList newTags;
    for (const QString& tag : tagsAfter) {
        if (!before.contains(tag)) {
            newTags.append(tag);
        }
    }
    if (!newTags.isEmpty()) {
        emit newTagsFound(repoName, newTags);
    }
}
