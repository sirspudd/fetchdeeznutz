#ifndef FETCHDEEZNUTZWINDOW_H
#define FETCHDEEZNUTZWINDOW_H

#include "gitmodels.h"
#include "gitfetchworker.h"
#include "gitutils.h"
#include "remoteselectiondialog.h"
#include "repositorydialog.h"
#include "repositorystore.h"
#include "repositorytreemodel.h"

#include <QMainWindow>
#include <QTreeView>
#include <QModelIndex>
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
    void onRepositoryItemDoubleClicked(const QModelIndex& index);
    void onFetchIntervalChanged();
    void onFetchTimeoutChanged();
    void onConnectionTimeoutChanged();
    void onAutoFetchToggled();
    void performScheduledFetch();
    void onBackgroundFetchStarted(const QString& repoName);
    void onBackgroundFetchProgress(const QString& repoName, const QString& remoteName, int progress);
    void onRemoteStatusChanged(const QString& repoName, const QString& remoteName, const QString& status);
    void onBackgroundFetchFinished(const QString& repoName, bool success, const QString& message);
    void onBackgroundFetchError(const QString& repoName, const QString& errorMessage);
    void onCommitCountsUpdated(const QString& repoName, const QString& remoteName, int commitsAhead, int commitsBehind);
    // Repaints in-flight remotes once per second so their elapsed counter ticks.
    void updateFetchElapsed();
    
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
    void calculateCommitCounts(GitRepository& repo);
    void calculateCommitCountsAsync(const GitRepository& repo);
    void scanDirectoryForRepositories(const QString& directoryPath);
    // Full structural rebuild of the tree (after add/remove/scan/load), keeping
    // the current selection where possible.
    void updateRepositoryTree();
    // Resolves the repository backing a model index (repository or remote node).
    GitRepository* repositoryForIndex(const QModelIndex& index);
    void loadSettings();
    void saveSettings();
    void updateAutoFetchControls();
    
    // Override close event to hide to tray
    void closeEvent(QCloseEvent *event) override;

    // UI components
    QTreeView *repositoryView;
    RepositoryTreeModel *repositoryModel;
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
    QCheckBox *startMinimizedCheckBox;

    QTextEdit *logTextEdit;

    // Background fetching
    QThread *fetchThread;
    GitFetchWorker *fetchWorker;
    QMap<QString, QProgressBar*> activeFetches;
    QGroupBox *fetchStatusGroup;
    QVBoxLayout *fetchStatusLayout;

    // Data
    RepositoryStore m_store;
    QList<GitRepository> repositories;
    QTimer *fetchTimer;
    QTimer *fetchTicker; // 1s heartbeat to animate elapsed time on active fetches
};

#endif // FETCHDEEZNUTZWINDOW_H
