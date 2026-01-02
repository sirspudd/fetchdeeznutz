#include "gitmodels.h"

QJsonObject GitRemote::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["url"] = url;
    obj["lastFetch"] = lastFetch;
    obj["status"] = status;
    obj["commitsAhead"] = commitsAhead;
    obj["commitsBehind"] = commitsBehind;
    return obj;
}

GitRemote GitRemote::fromJson(const QJsonObject& obj) {
    GitRemote remote;
    remote.name = obj["name"].toString();
    remote.url = obj["url"].toString();
    remote.lastFetch = obj["lastFetch"].toString();
    remote.status = obj["status"].toString();
    remote.commitsAhead = obj["commitsAhead"].toInt(0);
    remote.commitsBehind = obj["commitsBehind"].toInt(0);
    return remote;
}

QJsonObject GitRepository::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["localPath"] = localPath;
    obj["branch"] = branch;
    obj["fetchInterval"] = fetchInterval;
    obj["enabled"] = enabled;
    obj["lastFetch"] = lastFetch;
    obj["status"] = status;

    QJsonArray remotesArray;
    for (const GitRemote& remote : remotes) {
        remotesArray.append(remote.toJson());
    }
    obj["remotes"] = remotesArray;

    QJsonArray worktreesArray;
    for (const QString& worktree : worktrees) {
        worktreesArray.append(worktree);
    }
    obj["worktrees"] = worktreesArray;

    return obj;
}

GitRepository GitRepository::fromJson(const QJsonObject& obj) {
    GitRepository repo;
    repo.name = obj["name"].toString();
    repo.localPath = obj["localPath"].toString();
    repo.branch = obj["branch"].toString();
    repo.fetchInterval = obj["fetchInterval"].toInt(60);
    repo.enabled = obj["enabled"].toBool(true);
    repo.lastFetch = obj["lastFetch"].toString();
    repo.status = obj["status"].toString();

    // Handle legacy single URL format
    if (obj.contains("url") && !obj["url"].toString().isEmpty()) {
        GitRemote legacyRemote;
        legacyRemote.name = "origin";
        legacyRemote.url = obj["url"].toString();
        legacyRemote.status = "Ready";
        repo.remotes.append(legacyRemote);
    }

    // Load remotes array
    if (obj.contains("remotes") && obj["remotes"].isArray()) {
        QJsonArray remotesArray = obj["remotes"].toArray();
        for (const QJsonValue& value : remotesArray) {
            if (value.isObject()) {
                repo.remotes.append(GitRemote::fromJson(value.toObject()));
            }
        }
    }

    // Load worktrees array
    if (obj.contains("worktrees") && obj["worktrees"].isArray()) {
        QJsonArray worktreesArray = obj["worktrees"].toArray();
        for (const QJsonValue& value : worktreesArray) {
            if (value.isString()) {
                repo.worktrees.append(value.toString());
            }
        }
    }

    return repo;
}
