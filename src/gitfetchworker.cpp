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

    // The "connection timeout" setting doubles as the no-output (stall) timeout:
    // if git produces nothing for this long, we consider the remote stalled.
    const int idleSeconds = qMax(1, m_connectionTimeoutSeconds.load());

    QProcess proc;
    // Merge stderr (where git writes --progress sideband) into stdout so any
    // activity counts as liveness for stall detection.
    proc.setProcessChannelMode(QProcess::MergedChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    // Force SSH to be non-interactive and to bound its own connect phase, so a
    // dead host fails fast instead of blocking in a socket read forever. This is
    // the key behavior libgit2's SSH transport lacked.
    QString sshCmd = env.value(QStringLiteral("GIT_SSH_COMMAND"));
    if (sshCmd.isEmpty()) {
        sshCmd = QStringLiteral("ssh");
    }
    sshCmd += QStringLiteral(" -o BatchMode=yes -o ConnectTimeout=%1").arg(idleSeconds);
    env.insert(QStringLiteral("GIT_SSH_COMMAND"), sshCmd);
    proc.setProcessEnvironment(env);

    const QStringList args = {QStringLiteral("-C"), repoPath,
                              QStringLiteral("fetch"), QStringLiteral("--progress"),
                              remote.name};
    proc.start(QStringLiteral("git"), args);
    if (!proc.waitForStarted(5000)) {
        statusLabel = QStringLiteral("Error");
        return false;
    }

    auto lastActivity = std::chrono::steady_clock::now();
    enum class AbortReason { None, Stop, Deadline, Idle } reason = AbortReason::None;

    for (;;) {
        // Wait for output in small slices so we can poll stop/deadline/idle even
        // when git is quiet.
        if (proc.waitForReadyRead(200)) {
            const QByteArray chunk = proc.readAll();
            if (!chunk.isEmpty()) {
                lastActivity = std::chrono::steady_clock::now();
            }
        }

        if (proc.state() == QProcess::NotRunning) {
            proc.readAll(); // drain any trailing output
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        if (m_stopRequested.load()) { reason = AbortReason::Stop; break; }
        if (now >= deadline) { reason = AbortReason::Deadline; break; }
        if (now - lastActivity >= std::chrono::seconds(idleSeconds)) { reason = AbortReason::Idle; break; }
    }

    if (reason != AbortReason::None) {
        proc.kill();
        proc.waitForFinished(2000);
        switch (reason) {
            case AbortReason::Stop:     statusLabel = QStringLiteral("Cancelled"); break;
            case AbortReason::Deadline: statusLabel = QStringLiteral("Timeout"); break;
            case AbortReason::Idle:     statusLabel = QStringLiteral("Timeout"); break;
            default: break;
        }
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
