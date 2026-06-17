#ifndef FETCHDEEZNUTZWINDOW_H
#define FETCHDEEZNUTZWINDOW_H

#include "gitmodels.h"
#include "gitfetchworker.h"
#include "gitutils.h"
#include "remoteselectiondialog.h"
#include "repositorydialog.h"

#include <QMainWindow>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QCheckBox>
#include <QTextEdit>
#include <QTimer>
#include <QThread>
#include <QProgressBar>
#include <QMap>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QAction>
#include <QCloseEvent>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QDateTime>
#include <QFormLayout>
#include <QSettings>

// Forward declarations
struct git_repository;

class FetchDeeznutzWindow : public QMainWindow
{
    Q_OBJECT

public:
    FetchDeeznutzWindow(QWidget *parent = nullptr);
    ~FetchDeeznutzWindow();

private slots:
    void addRepository();
    void addDirectory();
    void editRepository();
    void removeRepository();
    void removeDirectory();
    void fetchSelected();
    void fetchAll();
    void onRepositorySelectionChanged();
    void onRepositoryItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onFetchIntervalChanged();
    void onFetchTimeoutChanged();
    void onConnectionTimeoutChanged();
    void onAutoFetchToggled();
    void performScheduledFetch();
    void onFetchFinished();
    void onFetchError(const QString& errorMessage);
    void onBackgroundFetchStarted(const QString& repoName);
    void onBackgroundFetchProgress(const QString& repoName, const QString& remoteName, int progress);
    void onBackgroundFetchFinished(const QString& repoName, bool success, const QString& message);
    void onBackgroundFetchError(const QString& repoName, const QString& errorMessage);
    void onCommitCountsUpdated(const QString& repoName, const QString& remoteName, int commitsAhead, int commitsBehind);
    
    // System tray slots
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void showWindow();
    void hideWindow();
    void quitApplication();
    void showContextMenu(const QPoint& pos);

private:
    void setupUI();
    void setupSystemTray();
    void loadRepositories();
    void saveRepositories();
    void startScheduledFetch();
    void stopScheduledFetch();
    void fetchRepository(GitRepository& repo);
    void logMessage(const QString& message);
    QString getConfigFilePath() const;
    void calculateCommitCounts(GitRepository& repo);
    void calculateCommitCountsAsync(const GitRepository& repo);
    void scanDirectoryForRepositories(const QString& directoryPath);
    QTreeWidgetItem* createPathTreeItem(const QString& path);
    QTreeWidgetItem* findOrCreatePathItem(const QString& path);
    void updateRepositoryTree();
    GitRepository* getRepositoryFromTreeItem(QTreeWidgetItem* item);
    QString generateRepositoryTooltip(const GitRepository& repo);
    void loadSettings();
    void saveSettings();
    void updateAutoFetchControls();
    
    // Override close event to hide to tray
    void closeEvent(QCloseEvent *event) override;

    // UI components
    QTreeWidget *repositoryTree;
    QMenu *contextMenu;
    QPushButton *addButton;
    QPushButton *addDirectoryButton;
    QPushButton *editButton;
    QPushButton *removeButton;
    QPushButton *fetchSelectedButton;
    QPushButton *fetchAllButton;

    QGroupBox *settingsGroup;
    QSpinBox *globalIntervalSpinBox;
    QSpinBox *fetchTimeoutSpinBox;
    QSpinBox *connectionTimeoutSpinBox;
    
    // System tray
    QSystemTrayIcon *trayIcon;
    QMenu *trayMenu;
    QAction *showAction;
    QAction *hideAction;
    QAction *quitAction;
    QCheckBox *autoFetchCheckBox;

    QTextEdit *logTextEdit;

    // Background fetching
    QThread *fetchThread;
    GitFetchWorker *fetchWorker;
    QMap<QString, QProgressBar*> activeFetches;
    QGroupBox *fetchStatusGroup;
    QVBoxLayout *fetchStatusLayout;

    // Data
    QList<GitRepository> repositories;
    QTimer *fetchTimer;
    int currentFetchIndex;
    bool isFetching;
};

#endif // FETCHDEEZNUTZWINDOW_H
