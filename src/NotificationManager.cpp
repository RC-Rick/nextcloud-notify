#include "NotificationManager.h"

#include <QApplication>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <canberra.h>

NotificationManager::NotificationManager(QObject *parent)
    : QObject(parent)
{
    loadFired();
}

NotificationManager::~NotificationManager()
{
    if (m_ca)
        ca_context_destroy(m_ca);
}

void NotificationManager::setEnabled(bool on) { m_enabled = on; }
void NotificationManager::setMode(int mode) { m_mode = mode; }
void NotificationManager::setSound(bool on) { m_sound = on; }

void NotificationManager::setThresholds(const QList<int> &minutes)
{
    m_thresholds = minutes;
    std::sort(m_thresholds.begin(), m_thresholds.end());
}

QString NotificationManager::keyFor(const EventOccurrence &ev, int thr) const
{
    return ev.uid + '|' + QString::number(ev.start.toSecsSinceEpoch()) + '|' + QString::number(thr);
}

void NotificationManager::loadFired()
{
    QSettings s;
    const QStringList stored = s.value(QStringLiteral("notify/fired")).toStringList();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const QString &k : stored) {
        const qint64 start = k.section('|', 1, 1).toLongLong();
        if (now - start < 7200) // храним 2 часа, старое выкидываем
            m_fired.insert(k);
    }
}

void NotificationManager::saveFired()
{
    QSettings s;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QStringList keep;
    for (const QString &k : m_fired) {
        const qint64 start = k.section('|', 1, 1).toLongLong();
        if (now - start < 7200)
            keep << k;
    }
    s.setValue(QStringLiteral("notify/fired"), keep);
}

void NotificationManager::check(const QList<EventOccurrence> &events)
{
    if (!m_enabled || m_mode == Off || m_thresholds.isEmpty())
        return;
    const QDateTime now = QDateTime::currentDateTime();
    bool changed = false;
    for (const EventOccurrence &ev : events) {
        if (ev.allDay)
            continue;
        const qint64 remaining = now.secsTo(ev.start);
        if (remaining <= 0)
            continue; // уже идёт или прошло — не предупреждаем
        bool firedOne = false;
        for (int thr : m_thresholds) { // по возрастанию
            if (remaining > qint64(thr) * 60)
                continue; // этот порог ещё не наступил
            const QString key = keyFor(ev, thr);
            if (m_fired.contains(key))
                continue;
            m_fired.insert(key);
            changed = true;
            if (!firedOne) {
                fire(ev, thr);   // самый близкий порог — реально уведомляем
                firedOne = true;
            }
            // более ранние пороги просто помечаем, чтобы не спамить задним числом
        }
    }
    if (changed)
        saveFired();
}

void NotificationManager::testNotification()
{
    EventOccurrence ev;
    ev.uid = QStringLiteral("test");
    ev.summary = QStringLiteral("Тестовое уведомление nc-notify");
    ev.location = QStringLiteral("проверка канала и звука");
    ev.start = QDateTime::currentDateTime().addSecs(15 * 60);
    ev.end = ev.start.addSecs(30 * 60);
    ev.allDay = false;
    const int thr = m_thresholds.isEmpty() ? 15 : m_thresholds.first();
    fire(ev, thr);
}

void NotificationManager::fire(const EventOccurrence &ev, int thresholdMin)
{
    if (m_mode == Both || m_mode == FlyOnly)
        sendFly(ev, thresholdMin);
    if (m_mode == Both || m_mode == WindowOnly)
        showPopup(ev, thresholdMin);
    if (m_sound)
        playSound();
}

void NotificationManager::sendFly(const EventOccurrence &ev, int thresholdMin)
{
    Q_UNUSED(thresholdMin);
    QString body = ev.start.toString(QStringLiteral("HH:mm"));
    if (ev.end.isValid() && ev.end > ev.start)
        body += QStringLiteral("–") + ev.end.toString(QStringLiteral("HH:mm"));
    if (!ev.location.isEmpty())
        body += QStringLiteral(" · ") + ev.location;
    const int leftMin = qMax(0, int((QDateTime::currentDateTime().secsTo(ev.start) + 30) / 60));
    body += QStringLiteral("\nНачало через %1 мин").arg(leftMin);

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("Notify"));
    QVariantList args;
    args << QStringLiteral("nc-notify");        // app_name
    args << uint(0);                            // replaces_id
    args << QStringLiteral("x-office-calendar");// app_icon
    args << ev.summary;                         // summary
    args << body;                               // body
    args << QStringList();                      // actions
    args << QVariantMap();                      // hints
    args << int(10000);                         // expire_timeout, ms
    msg.setArguments(args);
    QDBusConnection::sessionBus().asyncCall(msg);
}

void NotificationManager::showPopup(const EventOccurrence &ev, int thresholdMin)
{
    Q_UNUSED(thresholdMin);
    auto *w = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setStyleSheet(QStringLiteral(
        "QWidget#ncPopup{background:#2b2b2b;border:1px solid #3daee9;border-radius:8px;}"
        "QLabel{color:#eeeeee;}"
        "QPushButton{background:#3daee9;color:#ffffff;border:none;border-radius:4px;padding:4px 10px;}"
        "QPushButton:hover{background:#2a9fd8;}"));
    w->setObjectName(QStringLiteral("ncPopup"));
    w->setFixedWidth(340);

    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(14, 12, 14, 12);
    lay->setSpacing(6);

    auto *title = new QLabel(ev.summary, w);
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSize(tf.pointSize() + 1);
    title->setFont(tf);
    title->setWordWrap(true);

    QString sub = ev.start.toString(QStringLiteral("HH:mm"));
    if (ev.end.isValid() && ev.end > ev.start)
        sub += QStringLiteral("–") + ev.end.toString(QStringLiteral("HH:mm"));
    const int leftMin = qMax(0, int((QDateTime::currentDateTime().secsTo(ev.start) + 30) / 60));
    sub += QStringLiteral("  ·  через %1 мин").arg(leftMin);
    auto *time = new QLabel(sub, w);
    time->setStyleSheet(QStringLiteral("color:#9fd6ff;"));

    lay->addWidget(title);
    lay->addWidget(time);
    if (!ev.location.isEmpty()) {
        auto *loc = new QLabel(ev.location, w);
        loc->setStyleSheet(QStringLiteral("color:#bbbbbb;"));
        loc->setWordWrap(true);
        lay->addWidget(loc);
    }

    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *openBtn = new QPushButton(QStringLiteral("Открыть"), w);
    auto *closeBtn = new QPushButton(QStringLiteral("Закрыть"), w);
    btns->addWidget(openBtn);
    btns->addWidget(closeBtn);
    lay->addLayout(btns);

    const QUrl url = ev.webUrl;
    connect(openBtn, &QPushButton::clicked, w, [url, w]() {
        if (url.isValid())
            QDesktopServices::openUrl(url);
        w->close();
    });
    connect(closeBtn, &QPushButton::clicked, w, &QWidget::close);

    w->adjustSize();
    const QRect ag = QGuiApplication::primaryScreen()->availableGeometry();
    int margin = 10;
    for (const QPointer<QWidget> &p : m_popups)
        if (p)
            margin += p->height() + 8;
    w->move(ag.right() - w->width() - 12, ag.bottom() - w->height() - margin);
    m_popups.append(w);
    connect(w, &QObject::destroyed, this, [this]() {
        for (int i = m_popups.size() - 1; i >= 0; --i)
            if (!m_popups[i])
                m_popups.removeAt(i);
    });
    w->show();
    QTimer::singleShot(12000, w, &QWidget::close);
}

void NotificationManager::playSound()
{
    if (!m_ca) {
        if (ca_context_create(&m_ca) < 0) {
            m_ca = nullptr;
            return;
        }
        ca_context_change_props(m_ca,
                                CA_PROP_APPLICATION_NAME, "nc-notify",
                                CA_PROP_APPLICATION_ID, "nc-notify",
                                nullptr);
        ca_context_set_driver(m_ca, "pulse");
    }
    ca_context_play(m_ca, 0,
                    CA_PROP_EVENT_ID, "message-new-instant",
                    CA_PROP_EVENT_DESCRIPTION, "Nextcloud event reminder",
                    nullptr);
}
