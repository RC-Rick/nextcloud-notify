#pragma once

#include <QObject>
#include <QSystemTrayIcon>

class MainWindow;
class QMenu;

class TrayController : public QObject
{
    Q_OBJECT
public:
    explicit TrayController(MainWindow *window, QObject *parent = nullptr);
    bool isAvailable() const { return m_tray != nullptr; }
    QSystemTrayIcon *icon() const { return m_tray; }

private slots:
    void onActivated(QSystemTrayIcon::ActivationReason reason);

private:
    MainWindow *m_window = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
};
