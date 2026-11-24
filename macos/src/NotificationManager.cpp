#include "NotificationManager.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "MacNotify.h"

// macOS-версия движка напоминаний: канал «fly-notifications» заменён
// нативными баннерами Центра уведомлений, libcanberra — на afplay.

NotificationManager::NotificationManager(QObject *parent)
    : QObject(parent)
{
    MacNotify::initialize();
    loadFired();
}

NotificationManager::~NotificationManager() = default;

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
    const bool wantBanner = (m_mode == Both || m_mode == FlyOnly);
    const bool wantWindow = (m_mode == Both || m_mode == WindowOnly);

    bool bannerShown = false;
    if (wantBanner && MacNotify::canDeliver()) {
        QString body = ev.start.toString(QStringLiteral("HH:mm"));
        if (ev.end.isValid() && ev.end > ev.start)
            body += QStringLiteral("–") + ev.end.toString(QStringLiteral("HH:mm"));
        if (!ev.location.isEmpty())
            body += QStringLiteral(" · ") + ev.location;
        const int leftMin = qMax(0, int((QDateTime::currentDateTime().secsTo(ev.start) + 30) / 60));
        body += QStringLiteral("\nНачало через %1 мин").arg(leftMin);

        // Идентификатор баннера — по событию, порогу и времени начала,
        // чтобы повторная синхронизация не плодила дубликаты.
        bannerShown = MacNotify::send(ev.summary, body, keyFor(ev, thresholdMin),
                                      ev.webUrl.toString(), m_sound);
    }

    // Своё окно нужно либо по режиму, либо как запасной канал (нет разрешения
    // на уведомления, приложение запущено вне .app и т. п.).
    if (wantWindow || !bannerShown) {
        showPopup(ev, thresholdMin);
        if (m_sound && !bannerShown)
            playSound();
    }
}

void NotificationManager::showPopup(const EventOccurrence &ev, int thresholdMin)
{
    Q_UNUSED(thresholdMin);

    const bool dark = QGuiApplication::palette().color(QPalette::Window).lightness() < 128;

    auto *w = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint
                                       | Qt::WindowStaysOnTopHint
                                       | Qt::NoDropShadowWindowHint);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setAttribute(Qt::WA_TranslucentBackground);
    w->setObjectName(QStringLiteral("ncRoot"));
    w->setFixedWidth(360);

    auto *card = new QWidget(w);
    card->setObjectName(QStringLiteral("ncCard"));

    auto *outer = new QVBoxLayout(w);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(card);

    const QString bg = dark ? QStringLiteral("#2c2c2e") : QStringLiteral("#f6f6f6");
    const QString border = dark ? QStringLiteral("#3a3a3c") : QStringLiteral("#d8d8dc");
    const QString titleColor = dark ? QStringLiteral("#f2f2f7") : QStringLiteral("#1c1c1e");
    const QString subColor = dark ? QStringLiteral("#a1a1a6") : QStringLiteral("#6c6c70");
    const QString hintColor = dark ? QStringLiteral("#8e8e93") : QStringLiteral("#8a8a8e");
    const QString accent = dark ? QStringLiteral("#0a84ff") : QStringLiteral("#007aff");

    const QString sheet = QStringLiteral(
        "QWidget#ncCard{background:%1;border:1px solid %2;border-radius:12px;}"
        "QLabel#ncTitle{color:%3;}"
        "QLabel#ncTime{color:%4;}"
        "QLabel#ncLoc{color:%5;}"
        "QPushButton{color:%6;background:transparent;border:none;padding:2px 6px;}"
        "QPushButton:hover{text-decoration:underline;}").arg(bg, border, titleColor,
                                                            subColor, hintColor, accent);
    card->setStyleSheet(sheet);

    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(16, 14, 16, 10);
    lay->setSpacing(4);

    auto *title = new QLabel(ev.summary.isEmpty() ? QStringLiteral("(без названия)") : ev.summary,
                             card);
    title->setObjectName(QStringLiteral("ncTitle"));
    title->setWordWrap(true);
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSize(tf.pointSize() + 1);
    title->setFont(tf);

    QString sub = QStringLiteral("Nextcloud · ");
    if (ev.allDay) {
        sub += QStringLiteral("весь день");
    } else {
        sub += ev.start.toString(QStringLiteral("HH:mm"));
        if (ev.end.isValid() && ev.end > ev.start)
            sub += QStringLiteral("–") + ev.end.toString(QStringLiteral("HH:mm"));
        const int leftMin = qMax(0, int((QDateTime::currentDateTime().secsTo(ev.start) + 30) / 60));
        sub += QStringLiteral("  ·  через %1 мин").arg(leftMin);
    }
    auto *time = new QLabel(sub, card);
    time->setObjectName(QStringLiteral("ncTime"));

    lay->addWidget(title);
    lay->addWidget(time);
    if (!ev.location.isEmpty()) {
        auto *loc = new QLabel(ev.location, card);
        loc->setObjectName(QStringLiteral("ncLoc"));
        loc->setWordWrap(true);
        lay->addWidget(loc);
    }

    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *openBtn = new QPushButton(QStringLiteral("Открыть"), card);
    auto *closeBtn = new QPushButton(QStringLiteral("Закрыть"), card);
    btns->addWidget(openBtn);
    btns->addWidget(closeBtn);
    lay->addLayout(btns);

    const QUrl url = ev.webUrl;
    connect(openBtn, &QPushButton::clicked, card, [url, w]() {
        if (url.isValid())
            QDesktopServices::openUrl(url);
        w->close();
    });
    connect(closeBtn, &QPushButton::clicked, card, [w]() { w->close(); });

    w->adjustSize();
    const QRect ag = QGuiApplication::primaryScreen()->availableGeometry();
    int offset = 12;
    for (const QPointer<QWidget> &p : m_popups)
        if (p)
            offset += p->height() + 8;
    w->move(ag.right() - w->width() - 12, ag.top() + offset);
    m_popups.append(w);
    connect(w, &QObject::destroyed, this, [this]() {
        for (int i = m_popups.size() - 1; i >= 0; --i)
            if (!m_popups[i])
                m_popups.removeAt(i);
    });
    w->show();
    w->raise();
    QTimer::singleShot(12000, w, &QWidget::close);
}

void NotificationManager::playSound()
{
    QProcess::startDetached(QStringLiteral("/usr/bin/afplay"),
                            {QStringLiteral("/System/Library/Sounds/Glass.aiff")});
}
