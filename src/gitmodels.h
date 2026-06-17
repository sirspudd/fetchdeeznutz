#ifndef GITMODELS_H
#define GITMODELS_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

/**
 * Represents a Git remote with its status and commit differences
 */
struct GitRemote {
    QString name;
    QString url;
    QString lastFetch;
    QString status;
    int commitsAhead;
    int commitsBehind;
    // Transient: epoch-ms when this remote entered the "Fetching..." state, used
    // to render a live elapsed counter. Not persisted to JSON.
    qint64 fetchStartMs = 0;

    GitRemote() : commitsAhead(0), commitsBehind(0) {}

    QJsonObject toJson() const;
    static GitRemote fromJson(const QJsonObject& obj);
};

/**
 * Represents a Git repository with its configuration and remotes
 */
struct GitRepository {
    QString name;
    QString localPath;
    QString branch;
    int fetchInterval; // in minutes
    bool enabled;
    QString lastFetch;
    QString status;
    QList<GitRemote> remotes;
    QStringList worktrees; // List of worktree paths

    bool operator==(const GitRepository& other) const {
        return name == other.name && localPath == other.localPath;
    }

    QJsonObject toJson() const;
    static GitRepository fromJson(const QJsonObject& obj);
};

// Register types with Qt's meta-object system for use with Q_ARG
Q_DECLARE_METATYPE(GitRemote)
Q_DECLARE_METATYPE(GitRepository)

#endif // GITMODELS_H
