#ifndef REPOSITORYTREEMODEL_H
#define REPOSITORYTREEMODEL_H

#include "gitmodels.h"
#include <QAbstractItemModel>
#include <QList>
#include <QString>
#include <memory>
#include <vector>

/**
 * Tree model presenting the repository list as a three-level hierarchy:
 *
 *   Directory  (grouping by parent path)
 *     Repository
 *       Remote
 *
 * The model reads from a repository list owned by the caller (the window). For
 * structural changes (add/remove/scan/load) the caller invokes rebuild(); for
 * value-only changes (status, commit counts) it invokes the incremental
 * update methods, which emit dataChanged without resetting the model so the
 * view keeps its selection, expansion and scroll position.
 */
class RepositoryTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum class NodeType { Directory, Repository, Remote };

    explicit RepositoryTreeModel(const QList<GitRepository>* repositories, QObject* parent = nullptr);
    ~RepositoryTreeModel() override;

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /** Rebuild the whole tree from the repository list (full model reset). */
    void rebuild();

    /** Emit dataChanged for the repository row(s) with the given name. */
    void updateRepositoryStatus(const QString& repoName);
    /** Emit dataChanged for the matching remote row(s). */
    void updateRemoteCounts(const QString& repoName, const QString& remoteName);

    // Queries used by the view/controller
    bool isRepository(const QModelIndex& index) const;
    bool isDirectory(const QModelIndex& index) const;
    /** Index into the repository list for a Repository/Remote node, else -1. */
    int repositoryIndex(const QModelIndex& index) const;
    /** Directory path for a Directory node, else empty. */
    QString directoryPath(const QModelIndex& index) const;
    /** Model index for the repository identified by name+path, else invalid. */
    QModelIndex indexForRepository(const QString& name, const QString& localPath) const;

private:
    struct Node {
        NodeType type = NodeType::Directory;
        Node* parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
        int row = 0;            // position within parent's children
        QString dirPath;        // Directory nodes
        int repoIndex = -1;     // Repository / Remote nodes
        int remoteIndex = -1;   // Remote nodes
    };

    Node* nodeFromIndex(const QModelIndex& index) const;
    QModelIndex indexForNode(Node* node) const;
    void buildTree();
    QString displayText(const Node* node) const;
    QString toolTip(const Node* node) const;

    const QList<GitRepository>* m_repositories;
    std::unique_ptr<Node> m_root;
};

#endif // REPOSITORYTREEMODEL_H
