#include "fetchdeeznutzwindow.h"

#include <QApplication>
#include <QSystemTrayIcon>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    // Set application properties
    a.setApplicationName("FetchDeezNutz");
    a.setApplicationVersion("1.0");
    a.setOrganizationName("FetchDeezNutz");
    
    // Check if system tray is available
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, "System Tray",
                            "I couldn't detect any system tray on this system.");
        return 1;
    }
    
    // Don't quit when the last window is closed
    a.setQuitOnLastWindowClosed(false);
    
    FetchDeeznutzWindow w;
    // Don't show the window initially - it will be shown via system tray
    // w.show();
    
    return a.exec();
}
