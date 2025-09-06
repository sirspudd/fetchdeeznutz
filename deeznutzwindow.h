#ifndef DEEZNUTZWINDOW_H
#define DEEZNUTZWINDOW_H

#include <QMainWindow>
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

struct GitRepository {
    QString name;
    QString url;
    QString localPath;
    QString branch;
    int fetchInterval; // in minutes
    bool enabled;
    QString lastFetch;
    QString status;
    
    bool operator==(const GitRepository& other) const {
        return name == other.name && url == other.url && localPath == other.localPath;
    }
    
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["name"] = name;
        obj["url"] = url;
        obj["localPath"] = localPath;
        obj["branch"] = branch;
        obj["fetchInterval"] = fetchInterval;
        obj["enabled"] = enabled;
        obj["lastFetch"] = lastFetch;
        obj["status"] = status;
        return obj;
    }
    
    static GitRepository fromJson(const QJsonObject& obj) {
        GitRepository repo;
        repo.name = obj["name"].toString();
        repo.url = obj["url"].toString();
        repo.localPath = obj["localPath"].toString();
        repo.branch = obj["branch"].toString();
        repo.fetchInterval = obj["fetchInterval"].toInt(60);
        repo.enabled = obj["enabled"].toBool(true);
        repo.lastFetch = obj["lastFetch"].toString();
        repo.status = obj["status"].toString();
        return repo;
    }
};

class RepositoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RepositoryDialog(const GitRepository& repo = GitRepository(), QWidget *parent = nullptr);
    GitRepository getRepository() const;

private:
    QLineEdit *nameEdit;
    QLineEdit *urlEdit;
    QLineEdit *pathEdit;
    QLineEdit *branchEdit;
    QSpinBox *intervalSpinBox;
    QCheckBox *enabledCheckBox;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void addRepository();
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
    bool cloneRepository(const GitRepository& repo);
    bool isRepositoryValid(const QString& path);
    void logMessage(const QString& message);
    QString getConfigFilePath() const;
    QString getGitErrorMessage(int error) const;

    QListWidget *repositoryList;
    QPushButton *addButton;
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
