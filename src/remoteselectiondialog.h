#ifndef REMOTESELECTIONDIALOG_H
#define REMOTESELECTIONDIALOG_H

#include "gitmodels.h"
#include <QDialog>
#include <QList>
#include <QCheckBox>

class RemoteSelectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RemoteSelectionDialog(const QList<GitRemote>& remotes, QWidget *parent = nullptr);
    QList<GitRemote> getSelectedRemotes() const;

private slots:
    void selectAll();
    void selectNone();

private:
    QList<QCheckBox*> remoteCheckboxes;
    QList<GitRemote> allRemotes;
};

#endif // REMOTESELECTIONDIALOG_H
