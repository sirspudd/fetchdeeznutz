#ifndef GITFETCHWORKER_H
#define GITFETCHWORKER_H

#include "gitmodels.h"
#include <QObject>
#include <QThreadPool>
#include <git2.h>
#include <atomic>
#include <chrono>

class GitFetchWorker : public QObject
{
    Q_OBJECT

public:
    explicit GitFetchWorker(QObject *parent = nullptr);
    ~GitFetchWorker();

public slots:
    void fetchRepository(const GitRepository& repo);
    void stopFetching();
    void setTimeout(int timeoutSeconds);
    void setConnectionTimeout(int timeoutSeconds);

signals:
    void fetchStarted(const QString& repoName);
    void fetchProgress(const QString& repoName, const QString& remoteName, int progress);
    // Per-remote lifecycle so the UI can show exactly which remote is in flight:
    // status is one of "Queued", "Fetching...", "Success", "Error", "Timeout".
    void remoteStatusChanged(const QString& repoName, const QString& remoteName, const QString& status);
    void fetchFinished(const QString& repoName, bool success, const QString& message);
    void fetchError(const QString& repoName, const QString& errorMessage);
    void commitCountsUpdated(const QString& repoName, const QString& remoteName, int commitsAhead, int commitsBehind);
    // Emitted once per repository fetch when tags appeared that weren't present
    // before the fetch (regardless of which remote delivered them).
    void newTagsFound(const QString& repoName, const QStringList& tags);

private:
    // Fetch a single remote using its own repository handle (libgit2 handles are
    // not shareable across threads). Emits the "Fetching..." transition; returns
    // true on success and writes the resulting status label ("Success", "Error",
    // "Timeout", "Cancelled").
    bool fetchOneRemote(const QString& repoName, const QString& repoPath, const GitRemote& remote,
                        std::chrono::steady_clock::time_point deadline, QString& statusLabel);
    // Diff the repository's current tags against the pre-fetch snapshot and emit
    // newTagsFound for any that appeared.
    void checkForNewTags(const QString& repoName, const QString& repoPath, const QStringList& tagsBefore);
    QString getGitErrorMessage(int error) const;
    int sshKeyCallback(git_credential **out, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload);

    QThreadPool m_pool; // bounded pool so independent remote fetches run concurrently
    std::atomic<bool> m_stopRequested;
    std::atomic<int> m_timeoutSeconds;
    std::atomic<int> m_connectionTimeoutSeconds;
};

#endif // GITFETCHWORKER_H
