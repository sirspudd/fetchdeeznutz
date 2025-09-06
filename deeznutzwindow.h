#ifndef DEEZNUTZWINDOW_H
#define DEEZNUTZWINDOW_H

#include <QMainWindow>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QCheckBox>
#include <QTextEdit>
#include <QTimer>
#include <git2.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QFormLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>

struct GitRemote {
    QString name;
    QString url;
    QString lastFetch;
    QString status;
    int commitsAhead;
    int commitsBehind;
    
    GitRemote() : commitsAhead(0), commitsBehind(0) {}
    
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["name"] = name;
        obj["url"] = url;
        obj["lastFetch"] = lastFetch;
        obj["status"] = status;
        obj["commitsAhead"] = commitsAhead;
        obj["commitsBehind"] = commitsBehind;
        return obj;
    }
    
    static GitRemote fromJson(const QJsonObject& obj) {
        GitRemote remote;
        remote.name = obj["name"].toString();
        remote.url = obj["url"].toString();
        remote.lastFetch = obj["lastFetch"].toString();
        remote.status = obj["status"].toString();
        remote.commitsAhead = obj["commitsAhead"].toInt(0);
        remote.commitsBehind = obj["commitsBehind"].toInt(0);
        return remote;
    }
};

struct GitRepository {
    QString name;
    QString localPath;
    QString branch;
    int fetchInterval; // in minutes
    bool enabled;
    QString lastFetch;
    QString status;
    QList<GitRemote> remotes;
    
    bool operator==(const GitRepository& other) const {
        return name == other.name && localPath == other.localPath;
    }
    
    QJsonObject toJson() const {
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
        
        return obj;
    }
    
    static GitRepository fromJson(const QJsonObject& obj) {
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
        
        return repo;
    }
};

class RepositoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RepositoryDialog(const GitRepository& repo = GitRepository(), QWidget *parent = nullptr);
    GitRepository getRepository() const;

private slots:
    void addRemote();
    void removeRemote();
    void onRemoteSelectionChanged();

private:
    QLineEdit *nameEdit;
    QLineEdit *pathEdit;
    QLineEdit *branchEdit;
    QSpinBox *intervalSpinBox;
    QCheckBox *enabledCheckBox;
    QListWidget *remotesList;
    QPushButton *addRemoteButton;
    QPushButton *removeRemoteButton;
    QLineEdit *remoteNameEdit;
    QLineEdit *remoteUrlEdit;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void addRepository();
    void addDirectory();
    void editRepository();
    void removeRepository();
    void fetchSelected();
    void fetchAll();
    void onRepositorySelectionChanged();
    void onFetchIntervalChanged();
    void onAutoFetchToggled();
    void performScheduledFetch();
    void onFetchFinished();
    void onFetchError(const QString& errorMessage);

private:
    void setupUI();
    void loadRepositories();
    void saveRepositories();
    void updateRepositoryList();
    void startScheduledFetch();
    void stopScheduledFetch();
    void fetchRepository(GitRepository& repo);
    bool isRepositoryValid(const QString& path);
    void logMessage(const QString& message);
    QString getConfigFilePath() const;
    QString getGitErrorMessage(int error) const;
    int sshKeyCallback(git_credential **out, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload);
    void calculateCommitCounts(GitRepository& repo);
    void calculateRemoteCommitCounts(git_repository* repository, GitRemote& remote, const QString& branch);
    void scanDirectoryForRepositories(const QString& directoryPath);
    QTreeWidgetItem* createPathTreeItem(const QString& path);
    QTreeWidgetItem* findOrCreatePathItem(const QString& path);
    void updateRepositoryTree();
    GitRepository* getRepositoryFromTreeItem(QTreeWidgetItem* item);
    QStringList findGitRepositories(const QString& directoryPath, const QStringList& excludeDirs = QStringList());
    bool isGitRepository(const QString& path);
    QString getRepositoryName(const QString& path);
    QList<GitRemote> getRepositoryRemotes(const QString& path);
    QString getRepositoryBranch(const QString& path);

    QTreeWidget *repositoryTree;
    QPushButton *addButton;
    QPushButton *addDirectoryButton;
    QPushButton *editButton;
    QPushButton *removeButton;
    QPushButton *fetchSelectedButton;
    QPushButton *fetchAllButton;
    
    QGroupBox *settingsGroup;
    QSpinBox *globalIntervalSpinBox;
    QCheckBox *autoFetchCheckBox;
    
    QTextEdit *logTextEdit;
    
    QList<GitRepository> repositories;
    QTimer *fetchTimer;
    int currentFetchIndex;
    bool isFetching;
};

#endif // DEEZNUTZWINDOW_H
