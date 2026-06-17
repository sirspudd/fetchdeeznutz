#include "repositorytreemodel.h"

#include <QFileInfo>
#include <QMap>

RepositoryTreeModel::RepositoryTreeModel(const QList<GitRepository>* repositories, QObject* parent)
    : QAbstractItemModel(parent)
    , m_repositories(repositories)
    , m_root(std::make_unique<Node>())
{
    buildTree();
}

RepositoryTreeModel::~RepositoryTreeModel() = default;

void RepositoryTreeModel::buildTree()
{
    m_root = std::make_unique<Node>();
    m_root->type = NodeType::Directory;

    if (!m_repositories) {
        return;
    }

    // Group repository indices by their parent directory, keeping directories
    // ordered (QMap is sorted by key) to match the previous tree ordering.
    QMap<QString, QList<int>> pathToRepos;
    for (int i = 0; i < m_repositories->size(); ++i) {
        const QString dirPath = QFileInfo(m_repositories->at(i).localPath).absolutePath();
        pathToRepos[dirPath].append(i);
    }

    for (auto it = pathToRepos.constBegin(); it != pathToRepos.constEnd(); ++it) {
        auto dirNode = std::make_unique<Node>();
        dirNode->type = NodeType::Directory;
        dirNode->dirPath = it.key();
        dirNode->parent = m_root.get();
        dirNode->row = static_cast<int>(m_root->children.size());

        for (int repoIndex : it.value()) {
            auto repoNode = std::make_unique<Node>();
            repoNode->type = NodeType::Repository;
            repoNode->repoIndex = repoIndex;
            repoNode->parent = dirNode.get();
            repoNode->row = static_cast<int>(dirNode->children.size());

            const GitRepository& repo = m_repositories->at(repoIndex);
            for (int r = 0; r < repo.remotes.size(); ++r) {
                auto remoteNode = std::make_unique<Node>();
                remoteNode->type = NodeType::Remote;
                remoteNode->repoIndex = repoIndex;
                remoteNode->remoteIndex = r;
                remoteNode->parent = repoNode.get();
                remoteNode->row = static_cast<int>(repoNode->children.size());
                repoNode->children.push_back(std::move(remoteNode));
            }

            dirNode->children.push_back(std::move(repoNode));
        }

        m_root->children.push_back(std::move(dirNode));
    }
}

void RepositoryTreeModel::rebuild()
{
    beginResetModel();
    buildTree();
    endResetModel();
}

RepositoryTreeModel::Node* RepositoryTreeModel::nodeFromIndex(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return m_root.get();
    }
    return static_cast<Node*>(index.internalPointer());
}

QModelIndex RepositoryTreeModel::indexForNode(Node* node) const
{
    if (!node || node == m_root.get()) {
        return QModelIndex();
    }
    return createIndex(node->row, 0, node);
}

QModelIndex RepositoryTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }
    Node* parentNode = nodeFromIndex(parent);
    if (row < 0 || row >= static_cast<int>(parentNode->children.size())) {
        return QModelIndex();
    }
    return createIndex(row, column, parentNode->children[row].get());
}

QModelIndex RepositoryTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid()) {
        return QModelIndex();
    }
    Node* node = nodeFromIndex(child);
    return indexForNode(node->parent);
}

int RepositoryTreeModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0) {
        return 0;
    }
    Node* node = nodeFromIndex(parent);
    return static_cast<int>(node->children.size());
}

int RepositoryTreeModel::columnCount(const QModelIndex& /*parent*/) const
{
    return 1;
}

QVariant RepositoryTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    const Node* node = nodeFromIndex(index);

    switch (role) {
    case Qt::DisplayRole:
        return displayText(node);
    case Qt::ToolTipRole:
        return toolTip(node);
    default:
        return QVariant();
    }
}

QVariant RepositoryTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 0) {
        return QStringLiteral("Repositories");
    }
    return QVariant();
}

QString RepositoryTreeModel::displayText(const Node* node) const
{
    if (!node || !m_repositories) {
        return QString();
    }

    if (node->type == NodeType::Directory) {
        return node->dirPath;
    }

    if (node->type == NodeType::Repository) {
        const GitRepository& repo = m_repositories->at(node->repoIndex);
        QString statusIcon = repo.enabled ? QStringLiteral("\u25CF") : QStringLiteral("\u25CB");
        const QString statusText = repo.status.isEmpty() ? QStringLiteral("Ready") : repo.status;

        if (statusText == "Timeout") {
            statusIcon = QStringLiteral("\u23F0");
        } else if (statusText == "Error") {
            statusIcon = QStringLiteral("\u274C");
        } else if (statusText == "Success") {
            statusIcon = QStringLiteral("\u2705");
        } else if (statusText == "Fetching...") {
            statusIcon = QStringLiteral("\U0001F504");
        }

        return QStringLiteral("%1 %2 - %3 (%4) [%5 remotes]")
            .arg(statusIcon, repo.name, statusText, repo.branch)
            .arg(repo.remotes.size());
    }

    // Remote node
    const GitRepository& repo = m_repositories->at(node->repoIndex);
    const GitRemote& remote = repo.remotes.at(node->remoteIndex);

    QString remoteStatusIcon = QStringLiteral("\u25CF");
    const QString remoteStatus = remote.status.isEmpty() ? QStringLiteral("Ready") : remote.status;
    if (remoteStatus == "Error") {
        remoteStatusIcon = QStringLiteral("\u274C");
    } else if (remoteStatus == "Success") {
        remoteStatusIcon = QStringLiteral("\u2705");
    } else if (remoteStatus == "Fetching...") {
        remoteStatusIcon = QStringLiteral("\U0001F504");
    } else if (remoteStatus == "Timeout") {
        remoteStatusIcon = QStringLiteral("\u23F0");
    } else if (remoteStatus == "Queued") {
        remoteStatusIcon = QStringLiteral("\u23F3");
    } else if (remoteStatus == "Cancelled") {
        remoteStatusIcon = QStringLiteral("\u26A0");
    }

    QString delta;
    if (remote.commitsAhead > 0 && remote.commitsBehind > 0) {
        delta = QStringLiteral(" [+%1/-%2]").arg(remote.commitsAhead).arg(remote.commitsBehind);
    } else if (remote.commitsAhead > 0) {
        delta = QStringLiteral(" [+%1]").arg(remote.commitsAhead);
    } else if (remote.commitsBehind > 0) {
        delta = QStringLiteral(" [-%1]").arg(remote.commitsBehind);
    } else {
        delta = QStringLiteral(" [up-to-date]");
    }

    // Surface transient/failure states inline so a stalled remote is obvious at
    // a glance (it sits on "Fetching..." while siblings move to Success/Error).
    QString statusSuffix;
    if (remoteStatus != "Ready" && remoteStatus != "Success") {
        statusSuffix = QStringLiteral(" - %1").arg(remoteStatus);
    }

    return QStringLiteral("%1 %2 - %3%4%5").arg(remoteStatusIcon, remote.name, remote.url, delta, statusSuffix);
}

QString RepositoryTreeModel::toolTip(const Node* node) const
{
    if (!node || !m_repositories) {
        return QString();
    }

    if (node->type == NodeType::Remote) {
        const GitRepository& repo = m_repositories->at(node->repoIndex);
        const GitRemote& remote = repo.remotes.at(node->remoteIndex);
        QString tip = QStringLiteral("Remote: %1\nURL: %2\nStatus: %3\nAhead: %4 commits\nBehind: %5 commits")
                          .arg(remote.name, remote.url,
                               remote.status.isEmpty() ? QStringLiteral("Ready") : remote.status)
                          .arg(remote.commitsAhead)
                          .arg(remote.commitsBehind);
        if (!remote.lastFetch.isEmpty()) {
            tip += QStringLiteral("\nLast fetch: %1").arg(remote.lastFetch);
        }
        return tip;
    }

    if (node->type != NodeType::Repository) {
        return QString();
    }

    const GitRepository& repo = m_repositories->at(node->repoIndex);
    QString tooltip = QStringLiteral("<b>%1</b><br/>").arg(repo.name);
    tooltip += QStringLiteral("Path: %1<br/>").arg(repo.localPath);
    tooltip += QStringLiteral("Branch: %1<br/>").arg(repo.branch);
    tooltip += QStringLiteral("Status: %1<br/>").arg(repo.status.isEmpty() ? QStringLiteral("Ready") : repo.status);
    if (!repo.lastFetch.isEmpty()) {
        tooltip += QStringLiteral("Last Fetch: %1<br/>").arg(repo.lastFetch);
    }
    tooltip += QStringLiteral("Fetch Interval: %1 minutes<br/>").arg(repo.fetchInterval);
    tooltip += QStringLiteral("Enabled: %1<br/><br/>").arg(repo.enabled ? QStringLiteral("Yes") : QStringLiteral("No"));

    if (repo.remotes.isEmpty()) {
        tooltip += QStringLiteral("<b>No remotes configured</b>");
    } else {
        tooltip += QStringLiteral("<b>Remotes (%1):</b><br/>").arg(repo.remotes.size());
        for (const GitRemote& remote : repo.remotes) {
            tooltip += QStringLiteral("\u2022 <b>%1</b><br/>").arg(remote.name);
            tooltip += QStringLiteral("  URL: %1<br/>").arg(remote.url);
            tooltip += QStringLiteral("  Status: %1<br/>").arg(remote.status.isEmpty() ? QStringLiteral("Ready") : remote.status);
            if (remote.commitsAhead > 0 || remote.commitsBehind > 0) {
                tooltip += QStringLiteral("  Commits: ");
                if (remote.commitsAhead > 0) {
                    tooltip += QStringLiteral("+%1 ahead").arg(remote.commitsAhead);
                }
                if (remote.commitsAhead > 0 && remote.commitsBehind > 0) {
                    tooltip += QStringLiteral(", ");
                }
                if (remote.commitsBehind > 0) {
                    tooltip += QStringLiteral("-%1 behind").arg(remote.commitsBehind);
                }
                tooltip += QStringLiteral("<br/>");
            }
            if (!remote.lastFetch.isEmpty()) {
                tooltip += QStringLiteral("  Last Fetch: %1<br/>").arg(remote.lastFetch);
            }
        }
    }
    return tooltip;
}

void RepositoryTreeModel::updateRepositoryStatus(const QString& repoName)
{
    if (!m_repositories) {
        return;
    }
    for (const auto& dir : m_root->children) {
        for (const auto& repoNode : dir->children) {
            if (m_repositories->at(repoNode->repoIndex).name == repoName) {
                const QModelIndex idx = indexForNode(repoNode.get());
                emit dataChanged(idx, idx);
            }
        }
    }
}

void RepositoryTreeModel::updateRemoteCounts(const QString& repoName, const QString& remoteName)
{
    if (!m_repositories) {
        return;
    }
    for (const auto& dir : m_root->children) {
        for (const auto& repoNode : dir->children) {
            const GitRepository& repo = m_repositories->at(repoNode->repoIndex);
            if (repo.name != repoName) {
                continue;
            }
            for (const auto& remoteNode : repoNode->children) {
                if (repo.remotes.at(remoteNode->remoteIndex).name == remoteName) {
                    const QModelIndex idx = indexForNode(remoteNode.get());
                    emit dataChanged(idx, idx);
                }
            }
        }
    }
}

bool RepositoryTreeModel::isRepository(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return false;
    }
    return nodeFromIndex(index)->type == NodeType::Repository;
}

bool RepositoryTreeModel::isDirectory(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return false;
    }
    return nodeFromIndex(index)->type == NodeType::Directory;
}

int RepositoryTreeModel::repositoryIndex(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return -1;
    }
    return nodeFromIndex(index)->repoIndex;
}

QString RepositoryTreeModel::directoryPath(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return QString();
    }
    const Node* node = nodeFromIndex(index);
    return node->type == NodeType::Directory ? node->dirPath : QString();
}

QModelIndex RepositoryTreeModel::indexForRepository(const QString& name, const QString& localPath) const
{
    if (!m_repositories) {
        return QModelIndex();
    }
    for (const auto& dir : m_root->children) {
        for (const auto& repoNode : dir->children) {
            const GitRepository& repo = m_repositories->at(repoNode->repoIndex);
            if (repo.name == name && repo.localPath == localPath) {
                return indexForNode(repoNode.get());
            }
        }
    }
    return QModelIndex();
}
