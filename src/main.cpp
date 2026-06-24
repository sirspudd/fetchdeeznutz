#include "fetchdeeznutzwindow.h"

#include <QApplication>
#include <QSystemTrayIcon>
#include <QMessageBox>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    // Set application properties
    a.setApplicationName("FetchDeezNutz");
    a.setApplicationVersion("1.0");
    a.setOrganizationName("FetchDeezNutz");

    // Link the running app to its installed .desktop entry. On Wayland this sets
    // the surface app_id, which is how the compositor/KDE maps the window to the
    // installed launcher and its icon; it must match the installed file name
    // (fetchdeeznutz.desktop).
    a.setDesktopFileName(QStringLiteral("fetchdeeznutz"));

    // Window/taskbar icon: prefer the installed themed icon, falling back to the
    // bundled resource when running uninstalled (e.g. straight from the build dir).
    a.setWindowIcon(QIcon::fromTheme(QStringLiteral("fetchdeeznutz"), QIcon(QStringLiteral(":/nuts_icon.svg"))));
    
    // Check if system tray is available
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, "System Tray",
                            "I couldn't detect any system tray on this system.");
        return 1;
    }
    
    // Don't quit when the last window is closed
    a.setQuitOnLastWindowClosed(false);
    
    FetchDeeznutzWindow w;
    // The window manages its own startup visibility: it shows on launch unless
    // the "Start minimized to tray" setting is enabled.

    return a.exec();
}
