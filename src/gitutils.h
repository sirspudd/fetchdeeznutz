#ifndef GITUTILS_H
#define GITUTILS_H

#include "gitmodels.h"
#include <QString>
#include <QStringList>

namespace GitUtils {

/**
 * Result of running a git subprocess.
 *  - exitCode: the process exit code, or one of the negative sentinels below.
 *  - stdOut / stdErr: captured output (trimmed of trailing newline by callers
 *    as needed).
 */
struct GitResult {
    int exitCode = -1;
    QString stdOut;
    QString stdErr;

    bool ok() const { return exitCode == 0; }
};

// Sentinel exit codes for runGit() failures that aren't a normal git exit.
constexpr int GIT_PROCESS_FAILED_TO_START = -1001; // git binary missing / couldn't spawn
constexpr int GIT_PROCESS_TIMED_OUT = -1002;       // exceeded the supplied timeout

/**
 * Run `git <args>` in the given working directory and capture its output.
 *
 * This shells out to the system git so it shares the user's SSH config, agent,
 * credential helpers and known_hosts exactly like a manual `git` invocation.
 * The child runs with GIT_TERMINAL_PROMPT=0 (and SSH BatchMode) so it can never
 * block on an interactive credential/passphrase prompt.
 *
 * @param workingDir  repository working directory (passed via `git -C`).
 * @param args        git arguments (without the leading "git").
 * @param timeoutMs   hard wall-clock timeout; <=0 means no timeout. On timeout
 *                    the process is killed and exitCode is GIT_PROCESS_TIMED_OUT.
 */
GitResult runGit(const QString& workingDir, const QStringList& args, int timeoutMs = 30000);

/**
 * Check if a path is a valid Git repository or worktree.
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
 * Calculate commit counts (ahead/behind) for a remote, comparing the local
 * branch against the remote-tracking ref. Writes the results into `remote`.
 */
void calculateRemoteCommitCounts(const QString& repoPath, GitRemote& remote, const QString& branch, const QString& repoName);

/**
 * List the shorthand names of all tags in a repository (e.g. "v1.2.0").
 * Returns an empty list if the repository can't be read.
 */
QStringList listTags(const QString& repoPath);

/**
 * Check if a branch can be fast-forwarded (local is ancestor of remote)
 */
bool canFastForward(const QString& repoPath, const QString& branch, const QString& remoteName);

/**
 * Fast-forward the given branch to its remote-tracking branch (updates the
 * working tree). Returns false and sets errorMessage on failure.
 */
bool rebaseBranch(const QString& repoPath, const QString& branch, const QString& remoteName, QString& errorMessage);

} // namespace GitUtils

#endif // GITUTILS_H
