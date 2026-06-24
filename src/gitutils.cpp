#include "gitutils.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTextStream>
#include <QDebug>

namespace GitUtils {

GitResult runGit(const QString& workingDir, const QStringList& args, int timeoutMs) {
    GitResult result;

    QStringList fullArgs;
    if (!workingDir.isEmpty()) {
        fullArgs << QStringLiteral("-C") << workingDir;
    }
    fullArgs += args;

    QProcess proc;
    // Inherit the user's environment (PATH, HOME, SSH_AUTH_SOCK, ...) so git
    // behaves exactly like an interactive invocation, then layer on overrides
    // that guarantee it can never block on a prompt.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    proc.setProcessEnvironment(env);

    proc.start(QStringLiteral("git"), fullArgs);
    if (!proc.waitForStarted(5000)) {
        result.exitCode = GIT_PROCESS_FAILED_TO_START;
        result.stdErr = QStringLiteral("Failed to start git (is it installed and on PATH?)");
        return result;
    }

    if (!proc.waitForFinished(timeoutMs > 0 ? timeoutMs : -1)) {
        proc.kill();
        proc.waitForFinished(2000);
        result.exitCode = GIT_PROCESS_TIMED_OUT;
        result.stdErr = QStringLiteral("git timed out");
        return result;
    }

    result.stdOut = QString::fromUtf8(proc.readAllStandardOutput());
    result.stdErr = QString::fromUtf8(proc.readAllStandardError());
    result.exitCode = (proc.exitStatus() == QProcess::NormalExit) ? proc.exitCode() : GIT_PROCESS_FAILED_TO_START;
    return result;
}

bool isRepositoryValid(const QString& path) {
    return isGitRepository(path) || isGitWorktree(path);
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
                QString gitDir = content.mid(8).trimmed(); // Remove "gitdir: " prefix
                // The gitdir path may be relative to the worktree; resolve it.
                QDir worktreeDir(path);
                QString absoluteGitDir = QDir::cleanPath(worktreeDir.absoluteFilePath(gitDir));

                // A worktree's gitdir looks like:
                //   <mainRepo>/.git/worktrees/<name>
                // The main repository working tree is the path *before* "/.git/".
                const QString marker = QStringLiteral("/.git/worktrees/");
                int idx = absoluteGitDir.indexOf(marker);
                if (idx > 0) {
                    return absoluteGitDir.left(idx);
                }

                // Fallback: strip a trailing "/.git" component if present.
                int gitIdx = absoluteGitDir.lastIndexOf(QStringLiteral("/.git"));
                if (gitIdx > 0) {
                    return absoluteGitDir.left(gitIdx);
                }
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

    const GitResult listed = runGit(path, {QStringLiteral("remote")}, 10000);
    if (!listed.ok()) {
        return remotes;
    }

    const QStringList names = listed.stdOut.split('\n', Qt::SkipEmptyParts);
    for (const QString& rawName : names) {
        const QString name = rawName.trimmed();
        if (name.isEmpty()) {
            continue;
        }

        const GitResult url = runGit(path, {QStringLiteral("remote"), QStringLiteral("get-url"), name}, 10000);
        if (!url.ok()) {
            continue;
        }

        GitRemote gitRemote;
        gitRemote.name = name;
        gitRemote.url = url.stdOut.trimmed();
        gitRemote.status = QStringLiteral("Ready");
        if (!gitRemote.url.isEmpty()) {
            remotes.append(gitRemote);
        }
    }

    return remotes;
}

QString getRepositoryBranch(const QString& path) {
    const GitResult res = runGit(path, {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("HEAD")}, 10000);
    if (res.ok()) {
        const QString branch = res.stdOut.trimmed();
        if (!branch.isEmpty() && branch != QStringLiteral("HEAD")) {
            return branch;
        }
    }
    return QStringLiteral("main"); // Default / detached HEAD
}

void calculateRemoteCommitCounts(const QString& repoPath, GitRemote& remote, const QString& branch, const QString& repoName) {
    Q_UNUSED(repoName);

    remote.commitsAhead = 0;
    remote.commitsBehind = 0;

    // Resolve the local branch ref; fall back to HEAD if the named branch
    // doesn't exist locally.
    QString localRef = QStringLiteral("refs/heads/%1").arg(branch);
    if (!runGit(repoPath, {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("--quiet"), localRef}, 10000).ok()) {
        localRef = QStringLiteral("HEAD");
    }

    // Determine which remote-tracking ref to compare against.
    //  1. Prefer the configured upstream, but only if it belongs to THIS remote
    //     (comparing against an unrelated remote produces misleading counts).
    //  2. Otherwise use the same-named branch on this remote, if present.
    //  3. Otherwise there's nothing meaningful to compare -> 0/0.
    QString remoteRef;
    const GitResult upstream = runGit(repoPath,
        {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("--symbolic-full-name"),
         QStringLiteral("%1@{upstream}").arg(branch)}, 10000);
    if (upstream.ok()) {
        const QString up = upstream.stdOut.trimmed();
        if (up.startsWith(remote.name + QStringLiteral("/"))) {
            remoteRef = QStringLiteral("refs/remotes/%1").arg(up);
        }
    }

    if (remoteRef.isEmpty()) {
        const QString candidate = QStringLiteral("refs/remotes/%1/%2").arg(remote.name, branch);
        if (runGit(repoPath, {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("--quiet"), candidate}, 10000).ok()) {
            remoteRef = candidate;
        }
    }

    if (remoteRef.isEmpty()) {
        return; // no corresponding remote branch
    }

    // `git rev-list --left-right --count A...B` prints "<ahead> <behind>" where
    // ahead = commits in A not B and behind = commits in B not A.
    const GitResult counts = runGit(repoPath,
        {QStringLiteral("rev-list"), QStringLiteral("--left-right"), QStringLiteral("--count"),
         QStringLiteral("%1...%2").arg(localRef, remoteRef)}, 15000);
    if (!counts.ok()) {
        return;
    }

    const QStringList parts = counts.stdOut.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (parts.size() == 2) {
        remote.commitsAhead = parts[0].toInt();
        remote.commitsBehind = parts[1].toInt();
    }
}

QStringList listTags(const QString& repoPath) {
    const GitResult res = runGit(repoPath, {QStringLiteral("tag")}, 15000);
    if (!res.ok()) {
        return {};
    }
    return res.stdOut.split('\n', Qt::SkipEmptyParts);
}

bool canFastForward(const QString& repoPath, const QString& branch, const QString& remoteName) {
    const QString localRef = QStringLiteral("refs/heads/%1").arg(branch);
    const QString remoteRef = QStringLiteral("refs/remotes/%1/%2").arg(remoteName, branch);

    // Local can fast-forward to remote iff local is an ancestor of remote.
    // `merge-base --is-ancestor` exits 0 when true, 1 when false, other on error.
    const GitResult res = runGit(repoPath,
        {QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"), localRef, remoteRef}, 10000);
    return res.exitCode == 0;
}

bool rebaseBranch(const QString& repoPath, const QString& branch, const QString& remoteName, QString& errorMessage) {
    const QString remoteRef = QStringLiteral("refs/remotes/%1/%2").arg(remoteName, branch);

    if (!runGit(repoPath, {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("--quiet"), remoteRef}, 10000).ok()) {
        errorMessage = QStringLiteral("Remote branch %1/%2 not found").arg(remoteName, branch);
        return false;
    }

    // Make sure the branch we're fast-forwarding is the checked-out one, so the
    // working tree is updated to match (mirrors the previous behavior).
    const GitResult current = runGit(repoPath, {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("HEAD")}, 10000);
    if (current.stdOut.trimmed() != branch) {
        const GitResult checkout = runGit(repoPath, {QStringLiteral("checkout"), branch}, 30000);
        if (!checkout.ok()) {
            errorMessage = checkout.stdErr.trimmed().isEmpty()
                               ? QStringLiteral("Failed to checkout %1").arg(branch)
                               : checkout.stdErr.trimmed();
            return false;
        }
    }

    const GitResult merge = runGit(repoPath, {QStringLiteral("merge"), QStringLiteral("--ff-only"), remoteRef}, 30000);
    if (!merge.ok()) {
        errorMessage = merge.stdErr.trimmed().isEmpty()
                           ? QStringLiteral("Fast-forward failed")
                           : merge.stdErr.trimmed();
        return false;
    }

    return true;
}

} // namespace GitUtils
