#ifndef GITUTILS_H
#define GITUTILS_H

#include "gitmodels.h"
#include <QString>
#include <QStringList>
#include <QMutex>
#include <git2.h>

/**
 * Global mutex to serialize all libgit2 operations (libgit2 is not thread-safe)
 */
extern QMutex g_gitMutex;

/**
 * Custom error code for connection timeout
 */
#define GIT_ETIMEOUT -1000

namespace GitUtils {

/**
 * Check if a path is a valid Git repository
 */
bool isRepositoryValid(const QString& path);

/**
 * Check if a path is a Git repository (has .git directory)
 */
bool isGitRepository(const QString& path);

/**
 * Check if a path is a Git worktree
 */
bool isGitWorktree(const QString& path);

/**
 * Find the main repository path for a worktree
 */
QString findMainGitRepository(const QString& path);

/**
 * Find all worktrees for a given main repository
 */
QStringList findWorktreesForRepository(const QString& mainRepoPath);

/**
 * Recursively find all Git repositories in a directory
 */
QStringList findGitRepositories(const QString& directoryPath, const QStringList& excludeDirs = QStringList());

/**
 * Get the repository name from a path
 */
QString getRepositoryName(const QString& path);

/**
 * Get all remotes for a repository
 */
QList<GitRemote> getRepositoryRemotes(const QString& path);

/**
 * Get the current branch name for a repository
 */
QString getRepositoryBranch(const QString& path);

/**
 * Calculate commit counts (ahead/behind) for a remote
 */
void calculateRemoteCommitCounts(git_repository* repository, GitRemote& remote, const QString& branch, const QString& repoName);

/**
 * Get a human-readable error message from a libgit2 error code
 */
QString getGitErrorMessage(int error);

/**
 * Fetch from a remote with timeout (all operations protected by mutex)
 */
int fetchRemoteWithTimeout(git_remote* git_remote, const git_fetch_options& fetch_opts, int timeoutSeconds);

} // namespace GitUtils

#endif // GITUTILS_H
