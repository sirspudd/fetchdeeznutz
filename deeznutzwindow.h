#ifndef DEEZNUTZWINDOW_H
#define DEEZNUTZWINDOW_H

#include <QMainWindow>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
};
#endif // DEEZNUTZWINDOW_H
