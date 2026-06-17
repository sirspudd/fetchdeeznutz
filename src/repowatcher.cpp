#include "repowatcher.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

namespace {
constexpr int kDebounceMs = 400;

// Resolve "gitdir: <path>" from a worktree's .git file (or return the directory
// itself for a normal repository). Also resolves the shared common dir, where
// refs/heads and refs/remotes live for worktrees.
struct GitDirs {
    QString gitDir;    // per-worktree git dir (holds HEAD)
    QString commonDir; // shared dir (holds refs/heads, refs/remotes, packed-refs)
    bool valid = false;
};

QString firstLine(const QString& filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(f.readLine()).trimmed();
}

GitDirs resolveGitDirs(const QString& localPath)
{
    GitDirs dirs;
    const QString dotGit = localPath + QStringLiteral("/.git");
    QFileInfo fi(dotGit);

    if (fi.isDir()) {
        dirs.gitDir = dotGit;
        dirs.commonDir = dotGit;
        dirs.valid = true;
        return dirs;
    }

    if (fi.isFile()) {
        // Worktree: ".git" is a file containing "gitdir: <path>".
        const QString line = firstLine(dotGit);
        const QString marker = QStringLiteral("gitdir:");
        if (line.startsWith(marker)) {
            QString gd = line.mid(marker.size()).trimmed();
            if (QDir::isRelativePath(gd)) {
                gd = QDir(localPath).absoluteFilePath(gd);
            }
            dirs.gitDir = QDir(gd).absolutePath();

            // The shared dir is named in <gitDir>/commondir, else it's gitDir.
            const QString commonFile = dirs.gitDir + QStringLiteral("/commondir");
            if (QFileInfo::exists(commonFile)) {
                QString cd = firstLine(commonFile);
                if (QDir::isRelativePath(cd)) {
                    cd = QDir(dirs.gitDir).absoluteFilePath(cd);
                }
                dirs.commonDir = QDir(cd).absolutePath();
            } else {
                dirs.commonDir = dirs.gitDir;
            }
            dirs.valid = true;
        }
    }

    return dirs;
}
} // namespace

RepoWatcher::RepoWatcher(QObject *parent)
    : QObject(parent)
    , m_debounce(new QTimer(this))
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &RepoWatcher::flushPending);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &RepoWatcher::onPathChanged);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &RepoWatcher::onPathChanged);
}

void RepoWatcher::setRepositories(const QList<GitRepository>& repos)
{
    QStringList paths;
    paths.reserve(repos.size());
    for (const GitRepository& repo : repos) {
        paths.append(repo.localPath);
    }
    paths.sort();
    if (paths == m_lastPaths) {
        return; // repository set unchanged; keep existing watches
    }
    m_lastPaths = paths;
    rebuild(repos);
}

void RepoWatcher::rebuild(const QList<GitRepository>& repos)
{
    if (!m_watcher.files().isEmpty()) {
        m_watcher.removePaths(m_watcher.files());
    }
    if (!m_watcher.directories().isEmpty()) {
        m_watcher.removePaths(m_watcher.directories());
    }
    m_pathToRepo.clear();

    for (const GitRepository& repo : repos) {
        const QStringList targets = watchTargetsForRepo(repo.localPath);
        for (const QString& target : targets) {
            if (m_pathToRepo.contains(target)) {
                continue;
            }
            if (m_watcher.addPath(target)) {
                m_pathToRepo.insert(target, repo.name);
            }
        }
    }
}

QStringList RepoWatcher::watchTargetsForRepo(const QString& localPath) const
{
    const GitDirs dirs = resolveGitDirs(localPath);
    if (!dirs.valid) {
        return {};
    }

    QStringList targets;

    // Per-worktree HEAD (a file; re-armed on rename in onPathChanged).
    const QString head = dirs.gitDir + QStringLiteral("/HEAD");
    if (QFileInfo::exists(head)) {
        targets.append(head);
    }

    // packed-refs, if present (created lazily when refs get packed).
    const QString packed = dirs.commonDir + QStringLiteral("/packed-refs");
    if (QFileInfo::exists(packed)) {
        targets.append(packed);
    }

    // Watch the loose-ref directories (and their subdirs, for nested branch
    // names like "feature/foo"). Directory watches survive atomic-rename ref
    // updates, unlike watching the individual ref files.
    const QStringList refRoots = {
        dirs.commonDir + QStringLiteral("/refs/heads"),
        dirs.commonDir + QStringLiteral("/refs/remotes"),
    };
    for (const QString& root : refRoots) {
        if (!QFileInfo::exists(root)) {
            continue;
        }
        targets.append(root);
        QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            targets.append(it.next());
        }
    }

    return targets;
}

void RepoWatcher::onPathChanged(const QString& path)
{
    const auto it = m_pathToRepo.constFind(path);
    if (it != m_pathToRepo.constEnd()) {
        m_pending.insert(it.value());
    }

    // A git ref update replaces files via atomic rename, which drops the watch
    // on the old inode. Re-arm the path if it still exists and is no longer
    // tracked, so subsequent changes keep firing.
    if (QFileInfo::exists(path)
        && !m_watcher.files().contains(path)
        && !m_watcher.directories().contains(path)) {
        m_watcher.addPath(path);
    }

    m_debounce->start(); // (re)start: coalesces a burst of ref writes into one flush
}

void RepoWatcher::flushPending()
{
    const QSet<QString> pending = m_pending;
    m_pending.clear();
    for (const QString& repoName : pending) {
        emit repositoryChanged(repoName);
    }
}
