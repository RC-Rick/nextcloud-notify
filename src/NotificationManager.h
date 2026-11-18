#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QList>
#include <QPointer>
#include <QWidget>

#include "Event.h"

struct ca_context;

class NotificationManager : public QObject
{
    Q_OBJECT
public:
    enum Mode { Both = 0, FlyOnly = 1, WindowOnly = 2, Off = 3 };

    explicit NotificationManager(QObject *parent = nullptr);
    ~NotificationManager() override;

    void setEnabled(bool on);
    void setMode(int mode);
    void setSound(bool on);
    void setThresholds(const QList<int> &minutes);

    // Проверяет события и шлёт уведомления по порогам (с дедупликацией).
    void check(const QList<EventOccurrence> &events);
    // Принудительное тестовое уведомление (не зависит от enabled/mode,
    // но уважает выбранный канал и звук).
    void testNotification();

private:
    void fire(const EventOccurrence &ev, int thresholdMin);
    void sendFly(const EventOccurrence &ev, int thresholdMin);
    void showPopup(const EventOccurrence &ev, int thresholdMin);
    void playSound();
    QString keyFor(const EventOccurrence &ev, int thr) const;
    void loadFired();
    void saveFired();

    bool m_enabled = true;
    int m_mode = Both;
    bool m_sound = true;
    QList<int> m_thresholds;      // минуты, по возрастанию
    QSet<QString> m_fired;        // ключи уже отправленных (uid|startEpoch|thr)
    ca_context *m_ca = nullptr;
    QList<QPointer<QWidget>> m_popups;
};
