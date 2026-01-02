#ifndef REPOSITORYDIALOG_H
#define REPOSITORYDIALOG_H

#include "gitmodels.h"
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QListWidget>
#include <QPushButton>

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

#endif // REPOSITORYDIALOG_H
