#include "gitutils.h"
#include <git2.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>
#include <QMutexLocker>

// Global mutex to serialize all libgit2 operations
QMutex g_gitMutex;

namespace GitUtils {

bool isRepositoryValid(const QString& path) {
    git_repository *repository = nullptr;
    int error;
    {
        QMutexLocker locker(&g_gitMutex);
        error = git_repository_open(&repository, path.toLocal8Bit().constData());
    }

    if (error < 0) {
        return false;
    }

    {
        QMutexLocker locker(&g_gitMutex);
        git_repository_free(repository);
    }
    return true;
}

bool isGitRepository(const QString& path) {
    QDir dir(path);
    return dir.exists(".git");
}

bool isGitWorktree(const QString& path) {
    QDir dir(path);
    QFile gitFile(path + "/.git");
    
    if (!gitFile.exists()) {
        return false;
    }
    
    // Read the .git file to check if it's a worktree
    if (gitFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&gitFile);
        QString content = in.readAll().trimmed();
        gitFile.close();
        
        // Git worktrees have .git files that start with "gitdir: "
        return content.startsWith("gitdir: ");
    }
    
    return false;
}

QString findMainGitRepository(const QString& path) {
    if (isGitWorktree(path)) {
        QFile gitFile(path + "/.git");
        if (gitFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&gitFile);
            QString content = in.readAll().trimmed();
            gitFile.close();
            
            if (content.startsWith("gitdir: ")) {
                QString gitDir = content.mid(8); // Remove "gitdir: " prefix
                // The gitdir path is relative to the worktree, so we need to resolve it
                QDir worktreeDir(path);
                QString absoluteGitDir = worktreeDir.absoluteFilePath(gitDir);
                
                // The main repository is typically in the parent directory of the worktree's gitdir
                QDir gitDirObj(absoluteGitDir);
                return gitDirObj.absolutePath();
            }
        }
    }
    
    return path; // Return the original path if it's not a worktree
}

QStringList findWorktreesForRepository(const QString& mainRepoPath) {
    QStringList worktrees;
    
    // Check the main repository's worktrees directory
    QDir mainRepoDir(mainRepoPath);
    QDir worktreesDir(mainRepoDir.absoluteFilePath(".git/worktrees"));
    
    if (worktreesDir.exists()) {
        QFileInfoList entries = worktreesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries) {
            QString worktreePath = entry.absoluteFilePath();
            QFile gitFile(worktreePath + "/gitdir");
            
            if (gitFile.exists() && gitFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&gitFile);
                QString gitDirPath = in.readAll().trimmed();
                gitFile.close();
                
                // The gitdir file contains the path to the worktree's .git file
                // We need to find the actual worktree directory
                QFileInfo gitDirInfo(gitDirPath);
                if (gitDirInfo.exists()) {
                    QString worktreeDir = gitDirInfo.absolutePath();
                    worktrees.append(worktreeDir);
                }
            }
        }
    }
    
    return worktrees;
}

QStringList findGitRepositories(const QString& directoryPath, const QStringList& excludeDirs) {
    QStringList repositories;
    QDir dir(directoryPath);

    if (!dir.exists()) {
        return repositories;
    }

    // Check if current directory is a git repository or worktree
    if (isGitRepository(directoryPath) || isGitWorktree(directoryPath)) {
        // Find the main repository path to avoid duplicates
        QString mainRepoPath = findMainGitRepository(directoryPath);
        
        // Only add if we haven't seen this main repository before
        if (!repositories.contains(mainRepoPath)) {
            repositories.append(mainRepoPath);
        }
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
        QStringList subRepos = findGitRepositories(subDirPath, excludeDirs);
        
        // Add unique repositories only
        for (const QString& repo : subRepos) {
            if (!repositories.contains(repo)) {
                repositories.append(repo);
            }
        }
    }

    return repositories;
}

QString getRepositoryName(const QString& path) {
    QDir dir(path);
    return dir.dirName();
}

QList<GitRemote> getRepositoryRemotes(const QString& path) {
    QList<GitRemote> remotes;

    git_repository *repository = nullptr;
    int error;
    {
        QMutexLocker locker(&g_gitMutex);
        error = git_repository_open(&repository, path.toLocal8Bit().constData());
    }

    if (error < 0) {
        return remotes;
    }

    git_strarray remote_names = {0};
    {
        QMutexLocker locker(&g_gitMutex);
        error = git_remote_list(&remote_names, repository);
    }

    if (error >= 0) {
        for (size_t i = 0; i < remote_names.count; ++i) {
            git_remote *remote = nullptr;
            {
                QMutexLocker locker(&g_gitMutex);
                error = git_remote_lookup(&remote, repository, remote_names.strings[i]);
            }

            if (error >= 0) {
                GitRemote gitRemote;
                gitRemote.name = QString::fromUtf8(remote_names.strings[i]);

                const char *remote_url = nullptr;
                {
                    QMutexLocker locker(&g_gitMutex);
                    remote_url = git_remote_url(remote);
                }
                if (remote_url) {
                    gitRemote.url = QString::fromUtf8(remote_url);
                    gitRemote.status = "Ready";
                    remotes.append(gitRemote);
                }

                {
                    QMutexLocker locker(&g_gitMutex);
                    git_remote_free(remote);
                }
            }
        }

        git_strarray_free(&remote_names);
    }

    {
        QMutexLocker locker(&g_gitMutex);
        git_repository_free(repository);
    }
    return remotes;
}

QString getRepositoryBranch(const QString& path) {
    git_repository *repository = nullptr;
    int error;
    {
        QMutexLocker locker(&g_gitMutex);
        error = git_repository_open(&repository, path.toLocal8Bit().constData());
    }

    if (error < 0) {
        return "main"; // Default branch
    }

    git_reference *head = nullptr;
    {
        QMutexLocker locker(&g_gitMutex);
        error = git_repository_head(&head, repository);
    }

    QString branch = "main"; // Default
    if (error >= 0) {
        const char *branch_name = nullptr;
        {
            QMutexLocker locker(&g_gitMutex);
            branch_name = git_reference_shorthand(head);
        }
        if (branch_name) {
            branch = QString::fromUtf8(branch_name);
        }
        {
            QMutexLocker locker(&g_gitMutex);
            git_reference_free(head);
        }
    }

    {
        QMutexLocker locker(&g_gitMutex);
        git_repository_free(repository);
    }
    return branch;
}

void calculateRemoteCommitCounts(git_repository* repository, GitRemote& remote, const QString& branch, const QString& repoName) {
    qDebug() << "Calculating commit counts for" << repoName << "remote" << remote.name << "branch" << branch;
    
    QMutexLocker locker(&g_gitMutex);
    
    // Get the local branch reference
    QString localBranchRef = QString("refs/heads/%1").arg(branch);
    git_reference *localBranch = nullptr;
    int error = git_reference_lookup(&localBranch, repository, localBranchRef.toLocal8Bit().constData());

    if (error < 0) {
        qDebug() << "Local branch" << localBranchRef << "not found, trying HEAD";
        // Local branch doesn't exist, try HEAD
        error = git_repository_head(&localBranch, repository);
        if (error < 0) {
            qDebug() << "No HEAD found, setting counts to 0";
            remote.commitsAhead = 0;
            remote.commitsBehind = 0;
            return;
        }
    }

    // Get the remote branch reference - try multiple approaches
    git_reference *remoteBranch = nullptr;
    QString remoteBranchRef = QString("refs/remotes/%1/%2").arg(remote.name, branch);
    error = git_reference_lookup(&remoteBranch, repository, remoteBranchRef.toLocal8Bit().constData());

    if (error < 0) {
        qDebug() << "Remote branch" << remoteBranchRef << "not found, trying alternatives";
        
        // Try the remote's default branch (usually main or master)
        QStringList defaultBranches = {"main", "master", "develop"};
        for (const QString& defaultBranch : defaultBranches) {
            remoteBranchRef = QString("refs/remotes/%1/%2").arg(remote.name, defaultBranch);
            error = git_reference_lookup(&remoteBranch, repository, remoteBranchRef.toLocal8Bit().constData());
            if (error >= 0) {
                qDebug() << "Found remote branch" << remoteBranchRef;
                break;
            }
        }
        
        if (error < 0) {
            qDebug() << "No remote branch found, setting counts to 0";
            git_reference_free(localBranch);
            remote.commitsAhead = 0;
            remote.commitsBehind = 0;
            return;
        }
    } else {
        qDebug() << "Found remote branch" << remoteBranchRef;
    }

    // Get the commits
    git_commit *localCommit = nullptr;
    git_commit *remoteCommit = nullptr;

    error = git_commit_lookup(&localCommit, repository, git_reference_target(localBranch));
    if (error < 0) {
        qDebug() << "Failed to lookup local commit";
        git_reference_free(localBranch);
        git_reference_free(remoteBranch);
        remote.commitsAhead = 0;
        remote.commitsBehind = 0;
        return;
    }

    error = git_commit_lookup(&remoteCommit, repository, git_reference_target(remoteBranch));
    if (error < 0) {
        qDebug() << "Failed to lookup remote commit";
        git_commit_free(localCommit);
        git_reference_free(localBranch);
        git_reference_free(remoteBranch);
        remote.commitsAhead = 0;
        remote.commitsBehind = 0;
        return;
    }

    // Check if commits are the same
    if (git_oid_equal(git_commit_id(localCommit), git_commit_id(remoteCommit))) {
        qDebug() << "Local and remote commits are the same";
        remote.commitsAhead = 0;
        remote.commitsBehind = 0;
        git_commit_free(localCommit);
        git_commit_free(remoteCommit);
        git_reference_free(localBranch);
        git_reference_free(remoteBranch);
        return;
    }

    // Calculate ahead/behind counts using a more reliable method
    git_revwalk *walk = nullptr;
    error = git_revwalk_new(&walk, repository);
    if (error < 0) {
        qDebug() << "Failed to create revwalk";
        git_commit_free(localCommit);
        git_commit_free(remoteCommit);
        git_reference_free(localBranch);
        git_reference_free(remoteBranch);
        remote.commitsAhead = 0;
        remote.commitsBehind = 0;
        return;
    }

    git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL);

    // Count commits ahead (in local but not in remote)
    git_revwalk_push(walk, git_commit_id(localCommit));
    git_revwalk_hide(walk, git_commit_id(remoteCommit));

    int ahead = 0;
    git_oid oid;
    while (git_revwalk_next(&oid, walk) == 0) {
        ahead++;
    }

    // Count commits behind (in remote but not in local)
    git_revwalk_reset(walk);
    git_revwalk_push(walk, git_commit_id(remoteCommit));
    git_revwalk_hide(walk, git_commit_id(localCommit));

    int behind = 0;
    while (git_revwalk_next(&oid, walk) == 0) {
        behind++;
    }

    qDebug() << "Commit counts calculated - Ahead:" << ahead << "Behind:" << behind;

    remote.commitsAhead = ahead;
    remote.commitsBehind = behind;

    git_revwalk_free(walk);
    git_commit_free(localCommit);
    git_commit_free(remoteCommit);
    git_reference_free(localBranch);
    git_reference_free(remoteBranch);
}

QString getGitErrorMessage(int error) {
    if (error == GIT_ETIMEOUT) {
        return QString("Connection timeout");
    }
    
    const git_error *gitError = git_error_last();
    if (gitError && gitError->message) {
        return QString::fromUtf8(gitError->message);
    }
    
    return QString("Unknown error: %1").arg(error);
}

int fetchRemoteWithTimeout(git_remote* git_remote, const git_fetch_options& fetch_opts, int timeoutSeconds) {
    // All git operations must be protected by the global mutex for thread safety
    // The timeoutSeconds parameter is kept for API compatibility but not used
    // (libgit2 doesn't support cancelling operations mid-flight)
    QMutexLocker locker(&g_gitMutex);
    return git_remote_fetch(git_remote, nullptr, &fetch_opts, nullptr);
}

} // namespace GitUtils
