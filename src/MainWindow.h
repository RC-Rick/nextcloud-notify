#pragma once

#include <QMainWindow>

#include "Event.h"

class QCloseEvent;
class QSystemTrayIcon;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QTimer;
class CalDavClient;
class NotificationManager;
class DeckWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void setTrayIcon(QSystemTrayIcon *tray);

public slots:
    void showFromTray();
    void openSettings();
    void refresh();
    void showAbout();
    void showDeck();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onEvents(const QString &calUrl, const QList<EventOccurrence> &events);
    void onSyncError(const QString &ctx, const QString &msg);
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void checkNotifications();

private:
    void loadSettings();
    void loadNotifySettings();
    void applyAccount();
    void loadPassword();
    void savePassword(const QString &password);
    void rebuildTree();
    void updateTrayTooltip();
    void finishSync();
    void setStatus(const QString &text, bool error = false);

    QSystemTrayIcon *m_tray = nullptr;
    bool m_trayHintShown = false;

    CalDavClient *m_client = nullptr;
    NotificationManager *m_notify = nullptr;
    DeckWindow *m_deckWindow = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTimer *m_syncTimer = nullptr;
    QTimer *m_uiTimer = nullptr;
    QTimer *m_notifyTimer = nullptr;

    bool m_kerberos = false;
    QString m_url, m_user, m_password;
    QList<CalendarInfo> m_calendars;
    QList<EventOccurrence> m_events;
    int m_interval = 5;
    int m_horizon = 14;
    int m_pending = 0;
    bool m_syncing = false;
    QString m_lastError;
};
