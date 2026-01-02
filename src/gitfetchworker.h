#ifndef GITFETCHWORKER_H
#define GITFETCHWORKER_H

#include "gitmodels.h"
#include <QObject>
#include <QTimer>
#include <git2.h>

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
    void fetchFinished(const QString& repoName, bool success, const QString& message);
    void fetchError(const QString& repoName, const QString& errorMessage);
    void commitCountsUpdated(const QString& repoName, const QString& remoteName, int commitsAhead, int commitsBehind);

private:
    void performFetch(const GitRepository& repo);
    QString getGitErrorMessage(int error) const;
    int sshKeyCallback(git_credential **out, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload);
    void calculateRemoteCommitCounts(git_repository* repository, GitRemote& remote, const QString& branch, const QString& repoName);
    bool isRepositoryValid(const QString& path);
    int fetchRemoteWithTimeout(git_remote* git_remote, const git_fetch_options& fetch_opts, int timeoutSeconds);

    bool m_stopRequested;
    int m_timeoutSeconds;
    int m_connectionTimeoutSeconds;
};

#endif // GITFETCHWORKER_H
