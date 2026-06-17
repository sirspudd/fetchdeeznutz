#ifndef REPOWATCHER_H
#define REPOWATCHER_H

#include "gitmodels.h"

#include <QObject>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSet>
#include <QStringList>

class QTimer;

/**
 * Watches the on-disk refs of each tracked repository (via inotify, through
 * QFileSystemWatcher) and reports when they change outside the app - e.g. a
 * local commit, checkout, reset, or rebase. Emits repositoryChanged with the
 * repository name so the controller can recompute that repo's commit counts.
 *
 * Events are debounced because a single git operation touches several ref files
 * in quick succession; watched directories survive git's atomic-rename ref
 * updates, and individual ref files are re-armed if a rename drops their watch.
 */
class RepoWatcher : public QObject
{
    Q_OBJECT

public:
    explicit RepoWatcher(QObject *parent = nullptr);

    /**
     * Rebuild the set of watched paths to match the given repositories. Cheap to
     * call on every structural change; it no-ops when the repository paths are
     * unchanged.
     */
    void setRepositories(const QList<GitRepository>& repos);

signals:
    void repositoryChanged(const QString& repoName);

private slots:
    void onPathChanged(const QString& path);
    void flushPending();

private:
    void rebuild(const QList<GitRepository>& repos);
    // Resolve the ref paths worth watching for a repository working directory,
    // handling worktrees (per-worktree HEAD + shared common refs).
    QStringList watchTargetsForRepo(const QString& localPath) const;

    QFileSystemWatcher m_watcher;
    QHash<QString, QString> m_pathToRepo; // watched path -> repository name
    QStringList m_lastPaths;              // sorted localPaths of the last rebuild
    QSet<QString> m_pending;              // repo names awaiting a debounced flush
    QTimer *m_debounce;
};

#endif // REPOWATCHER_H
