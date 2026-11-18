#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QCloseEvent>
#include <QColor>
#include <QDesktopServices>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QSettings>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <qt5keychain/keychain.h>

#include <algorithm>

#include "CalDavClient.h"
#include "NotificationManager.h"
#include "SettingsDialog.h"
#include "DeckWindow.h"

static QString durationText(qint64 secs)
{
    if (secs < 0)
        secs = 0;
    const int days = static_cast<int>(secs / 86400);
    const int hours = static_cast<int>((secs % 86400) / 3600);
    const int mins = static_cast<int>((secs % 3600) / 60);
    if (days > 0)
        return QStringLiteral("%1 дн %2 ч").arg(days).arg(hours);
    if (hours > 0)
        return QStringLiteral("%1 ч %2 мин").arg(hours).arg(mins);
    return QStringLiteral("%1 мин").arg(mins);
}

static QString humanize(const QDateTime &now, const EventOccurrence &ev)
{
    if (ev.start <= now && now < ev.end)
        return QStringLiteral("идёт");
    if (ev.allDay) {
        const int days = now.date().daysTo(ev.start.date());
        return days <= 0 ? QStringLiteral("сегодня") : QStringLiteral("%1 дн").arg(days);
    }
    const qint64 secs = now.secsTo(ev.start);
    if (secs < 0)
        return QStringLiteral("—");
    return durationText(secs);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Nextcloud Notify"));
    resize(680, 520);

    QToolBar *tb = addToolBar(QStringLiteral("main"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QAction *aRefresh = tb->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                      QStringLiteral("Обновить"));
    connect(aRefresh, &QAction::triggered, this, &MainWindow::refresh);
    QAction *aSettings = tb->addAction(QIcon::fromTheme(QStringLiteral("configure")),
                                       QStringLiteral("Настройки"));
    connect(aSettings, &QAction::triggered, this, &MainWindow::openSettings);
    QAction *aAbout = tb->addAction(QIcon::fromTheme(QStringLiteral("help-about")),
                                    QStringLiteral("О программе"));
    connect(aAbout, &QAction::triggered, this, &MainWindow::showAbout);
    QAction *aDeck = tb->addAction(QIcon::fromTheme(QStringLiteral("view-list-details")),
                                   QStringLiteral("Карточки Deck"));
    connect(aDeck, &QAction::triggered, this, &MainWindow::showDeck);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({QStringLiteral("Время"), QStringLiteral("Событие"),
                             QStringLiteral("Календарь"), QStringLiteral("Через")});
    m_tree->setRootIsDecorated(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->setColumnWidth(0, 110);
    m_tree->setColumnWidth(2, 130);
    m_tree->setColumnWidth(3, 150);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &MainWindow::onItemDoubleClicked);
    setCentralWidget(m_tree);

    m_statusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusLabel);

    m_client = new CalDavClient(this);
    connect(m_client, &CalDavClient::eventsFetched, this, &MainWindow::onEvents);
    connect(m_client, &CalDavClient::requestFailed, this, &MainWindow::onSyncError);

    m_notify = new NotificationManager(this);
    m_deckWindow = new DeckWindow(m_notify, this);

    m_syncTimer = new QTimer(this);
    connect(m_syncTimer, &QTimer::timeout, this, &MainWindow::refresh);
    m_uiTimer = new QTimer(this);
    connect(m_uiTimer, &QTimer::timeout, this, &MainWindow::rebuildTree);
    m_uiTimer->start(60000);
    m_notifyTimer = new QTimer(this);
    connect(m_notifyTimer, &QTimer::timeout, this, &MainWindow::checkNotifications);
    m_notifyTimer->start(30000);

    QSettings s;
    m_trayHintShown = s.value(QStringLiteral("trayHintShown"), false).toBool();

    loadSettings();
    loadNotifySettings();
    rebuildTree();
    loadPassword();
    m_syncTimer->start();
}

void MainWindow::setTrayIcon(QSystemTrayIcon *tray)
{
    m_tray = tray;
}

void MainWindow::loadSettings()
{
    QSettings s;
    m_url = s.value(QStringLiteral("account/serverUrl")).toString();
    m_user = s.value(QStringLiteral("account/user")).toString();
    m_kerberos = s.value(QStringLiteral("account/authType"),
                         QStringLiteral("password")).toString() == QLatin1String("kerberos");
    m_client->setAuthMode(m_kerberos ? CalDavClient::KerberosAuth
                                     : CalDavClient::PasswordAuth);
    m_client->setCaFile(s.value(QStringLiteral("account/caFile")).toString());
    m_calendars.clear();
    const QStringList entries = s.value(QStringLiteral("account/calendars")).toStringList();
    for (const QString &e : entries)
        m_calendars << decodeCalendar(e);
    m_interval = s.value(QStringLiteral("sync/intervalMin"), 5).toInt();
    m_horizon = s.value(QStringLiteral("sync/horizonDays"), 14).toInt();
    m_syncTimer->setInterval(m_interval * 60000);
    applyAccount();
}

void MainWindow::loadNotifySettings()
{
    QSettings s;
    m_notify->setEnabled(s.value(QStringLiteral("notify/enabled"), true).toBool());
    m_notify->setMode(s.value(QStringLiteral("notify/mode"), int(NotificationManager::Both)).toInt());
    m_notify->setSound(s.value(QStringLiteral("notify/sound"), true).toBool());
    QList<int> thr;
    const QString raw = s.value(QStringLiteral("notify/thresholds"), QStringLiteral("15,5")).toString();
    const QStringList parts = raw.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        bool ok = false;
        const int v = p.toInt(&ok);
        if (ok && v > 0 && v <= 10080)
            thr << v;
    }
    std::sort(thr.begin(), thr.end());
    thr.erase(std::unique(thr.begin(), thr.end()), thr.end());
    if (thr.isEmpty())
        thr << 15 << 5;
    m_notify->setThresholds(thr);
}

void MainWindow::applyAccount()
{
    m_client->setAccount(m_url, m_user, m_password);
}

void MainWindow::loadPassword()
{
    auto *job = new QKeychain::ReadPasswordJob(QStringLiteral("nc-notify"), this);
    job->setAutoDelete(false);
    job->setKey(QStringLiteral("caldavPassword"));
    connect(job, &QKeychain::Job::finished, this, [this, job]() {
        if (job->error() == QKeychain::NoError) {
            m_password = job->textData();
            qInfo("nc-notify: keychain READ ok");
        } else if (job->error() == QKeychain::EntryNotFound) {
            qWarning("nc-notify: keychain READ: entry not found");
            if (!m_kerberos)
                setStatus(QStringLiteral("Пароль не найден в связке ключей — введите его в «Настройках»"), true);
        } else {
            qWarning("nc-notify: keychain READ error=%d %s",
                     int(job->error()), qPrintable(job->errorString()));
            setStatus(QStringLiteral("Связка ключей: %1").arg(job->errorString()), true);
        }
        job->deleteLater();
        applyAccount();
        rebuildTree();
        if (m_client->isConfigured() && !m_calendars.isEmpty())
            refresh();
    });
    job->start();
}

void MainWindow::savePassword(const QString &password)
{
    // Job живёт в MainWindow (не в диалоге!) — иначе умирает до конца async-записи.
    auto *job = new QKeychain::WritePasswordJob(QStringLiteral("nc-notify"), this);
    job->setAutoDelete(false);
    job->setKey(QStringLiteral("caldavPassword"));
    job->setTextData(password);
    connect(job, &QKeychain::Job::finished, this, [this, job]() {
        if (job->error() != QKeychain::NoError) {
            qWarning("nc-notify: keychain WRITE error=%d %s",
                     int(job->error()), qPrintable(job->errorString()));
            QMessageBox::warning(this, QStringLiteral("Связка ключей"),
                QStringLiteral("Не удалось сохранить пароль в связку ключей:\n%1\n"
                               "Пароль будет работать только до закрытия приложения.")
                    .arg(job->errorString()));
            setStatus(QStringLiteral("Связка ключей: запись не удалась — %1")
                          .arg(job->errorString()), true);
        } else {
            qInfo("nc-notify: keychain WRITE ok");
            setStatus(QStringLiteral("Пароль сохранён в связке ключей"));
        }
        job->deleteLater();
    });
    job->start();
}

void MainWindow::refresh()
{
    if (m_syncing)
        return;
    if (!m_client->isConfigured()) {
        if (!m_kerberos && !m_url.isEmpty() && !m_user.isEmpty() && m_password.isEmpty())
            setStatus(QStringLiteral("Пароль не загружен из связки ключей — откройте «Настройки»"), true);
        else
            setStatus(QStringLiteral("Укажите аккаунт в «Настройках»"), true);
        rebuildTree();
        return;
    }
    if (m_calendars.isEmpty()) {
        setStatus(QStringLiteral("Календари не выбраны — откройте «Настройки»"), true);
        return;
    }
    m_syncing = true;
    m_lastError.clear();
    m_events.clear();
    m_pending = m_calendars.count();
    setStatus(QStringLiteral("Синхронизация…"));
    const QDateTime from = QDate::currentDate().startOfDay();
    const QDateTime to = from.addDays(m_horizon);
    for (const CalendarInfo &cal : m_calendars)
        m_client->fetchEvents(cal, from, to);
}

void MainWindow::onEvents(const QString &calUrl, const QList<EventOccurrence> &events)
{
    Q_UNUSED(calUrl);
    m_events += events;
    if (m_pending > 0)
        --m_pending;
    if (m_pending == 0 && m_syncing)
        finishSync();
}

void MainWindow::onSyncError(const QString &ctx, const QString &msg)
{
    m_lastError = ctx + QStringLiteral(": ") + msg;
    if (m_pending > 0)
        --m_pending;
    if (m_pending == 0 && m_syncing)
        finishSync();
}

void MainWindow::finishSync()
{
    m_syncing = false;
    std::sort(m_events.begin(), m_events.end(),
              [](const EventOccurrence &a, const EventOccurrence &b) {
                  if (a.start != b.start)
                      return a.start < b.start;
                  return a.summary < b.summary;
              });
    rebuildTree();
    m_notify->check(m_events);
    if (m_lastError.isEmpty())
        setStatus(QStringLiteral("Синхронизировано %1 · событий: %2")
                      .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")))
                      .arg(m_events.count()));
    else
        setStatus(QStringLiteral("Ошибка: %1").arg(m_lastError), true);
}

void MainWindow::checkNotifications()
{
    m_notify->check(m_events);
}

void MainWindow::rebuildTree()
{
    m_tree->clear();
    if (!m_client->isConfigured()) {
        auto *it = new QTreeWidgetItem(m_tree);
        if (!m_kerberos && !m_url.isEmpty() && !m_user.isEmpty() && m_password.isEmpty())
            it->setText(0, QStringLiteral("Пароль не загружен из связки ключей. Откройте «Настройки» и введите пароль приложения."));
        else
            it->setText(0, QStringLiteral("Откройте «Настройки» и укажите сервер, логин и пароль приложения Nextcloud."));
        it->setFirstColumnSpanned(true);
        updateTrayTooltip();
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    const QDate today = now.date();
    const QDate tomorrow = today.addDays(1);

    QTreeWidgetItem *gNow = nullptr;
    QTreeWidgetItem *gToday = nullptr;
    QTreeWidgetItem *gTomorrow = nullptr;
    QTreeWidgetItem *gLater = nullptr;
    auto makeGroup = [this](const QString &title) {
        auto *g = new QTreeWidgetItem(m_tree);
        g->setText(0, title);
        QFont f = g->font(0);
        f.setBold(true);
        g->setFont(0, f);
        g->setFirstColumnSpanned(true);
        return g;
    };

    for (const EventOccurrence &ev : m_events) {
        const bool ongoing = ev.start <= now && now < ev.end;
        const bool past = ev.end <= now;
        const QDate d = ev.start.date();

        QTreeWidgetItem *group = nullptr;
        if (ongoing) {
            if (!gNow)
                gNow = makeGroup(QStringLiteral("Сейчас"));
            group = gNow;
        } else if (d == today) {
            if (!gToday)
                gToday = makeGroup(QStringLiteral("Сегодня, %1").arg(today.toString(QStringLiteral("dd.MM"))));
            group = gToday;
        } else if (d == tomorrow) {
            if (!gTomorrow)
                gTomorrow = makeGroup(QStringLiteral("Завтра, %1").arg(tomorrow.toString(QStringLiteral("dd.MM"))));
            group = gTomorrow;
        } else {
            if (!gLater)
                gLater = makeGroup(QStringLiteral("Позже"));
            group = gLater;
        }

        auto *it = new QTreeWidgetItem(group);
        QString timeText;
        if (ev.allDay)
            timeText = QStringLiteral("весь день");
        else if (ongoing && d != today)
            timeText = ev.start.toString(QStringLiteral("dd.MM HH:mm"));
        else if (d == today || d == tomorrow)
            timeText = ev.start.toString(QStringLiteral("HH:mm"));
        else
            timeText = ev.start.toString(QStringLiteral("dd.MM HH:mm"));
        it->setText(0, timeText);

        QString title = ev.summary.isEmpty() ? QStringLiteral("(без названия)") : ev.summary;
        if (!ev.location.isEmpty())
            title += QStringLiteral("  ·  ") + ev.location;
        it->setText(1, title);
        it->setText(2, ev.calendarName);
        it->setData(0, Qt::UserRole, ev.webUrl);
        it->setToolTip(1, QStringLiteral("%1\n%2 — %3\n%4")
                              .arg(ev.summary,
                                   ev.start.toString(QStringLiteral("dd.MM.yyyy HH:mm")),
                                   ev.end.toString(QStringLiteral("dd.MM.yyyy HH:mm")),
                                   ev.description.left(500)));

        if (ongoing) {
            QFont f = it->font(1);
            f.setBold(true);
            const QBrush green(QColor(0x2e, 0x7d, 0x32));
            for (int c = 0; c < 4; ++c) {
                it->setFont(c, f);
                it->setForeground(c, green);
            }
            if (ev.allDay) {
                it->setText(3, QStringLiteral("идёт · весь день"));
            } else {
                const qint64 total = ev.start.secsTo(ev.end);
                const qint64 done = ev.start.secsTo(now);
                auto *bar = new QProgressBar;
                bar->setRange(0, 100);
                bar->setValue(total > 0 ? qBound(0, static_cast<int>(done * 100 / total), 100) : 100);
                bar->setFixedHeight(16);
                bar->setTextVisible(true);
                bar->setFormat(QStringLiteral("осталось %1").arg(durationText(now.secsTo(ev.end))));
                m_tree->setItemWidget(it, 3, bar);
            }
        } else if (past) {
            const QBrush gray(QColor(0x80, 0x80, 0x80));
            for (int c = 0; c < 4; ++c)
                it->setForeground(c, gray);
            it->setText(3, QStringLiteral("завершено"));
        } else {
            it->setText(3, humanize(now, ev));
        }
    }
    if (m_events.isEmpty()) {
        auto *it = new QTreeWidgetItem(m_tree);
        it->setText(0, QStringLiteral("Нет событий в ближайшие %1 дн.").arg(m_horizon));
        it->setFirstColumnSpanned(true);
    }
    m_tree->expandAll();
    updateTrayTooltip();
}

void MainWindow::updateTrayTooltip()
{
    if (!m_tray)
        return;
    if (!m_client->isConfigured()) {
        m_tray->setToolTip(QStringLiteral("Nextcloud Notify — откройте «Настройки»"));
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    const EventOccurrence *current = nullptr;
    const EventOccurrence *next = nullptr;
    for (const EventOccurrence &ev : m_events) {
        if (ev.start <= now && now < ev.end) {
            if (!current || (!ev.allDay && current->allDay))
                current = &ev;
        } else if (ev.start > now) {
            if (!next || ev.start < next->start)
                next = &ev;
        }
    }
    QString tip = QStringLiteral("Nextcloud Notify");
    if (current) {
        tip += QStringLiteral("\nСейчас: %1").arg(current->summary);
        tip += current->allDay
                   ? QStringLiteral(" (весь день)")
                   : QStringLiteral(" (до %1)").arg(current->end.toString(QStringLiteral("HH:mm")));
    } else {
        tip += QStringLiteral("\nСейчас: свободно");
    }
    if (next) {
        const QString when = next->start.date() == now.date()
                                 ? next->start.toString(QStringLiteral("HH:mm"))
                                 : next->start.toString(QStringLiteral("dd.MM HH:mm"));
        tip += QStringLiteral("\nДалее: %1 — %2").arg(next->summary, when);
    }
    m_tray->setToolTip(tip);
}

void MainWindow::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item || !item->parent())
        return;
    const QUrl url = item->data(0, Qt::UserRole).toUrl();
    if (url.isValid())
        QDesktopServices::openUrl(url);
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(m_password, this);
    connect(&dlg, &SettingsDialog::testNotification, m_notify, &NotificationManager::testNotification);
    if (dlg.exec() == QDialog::Accepted) {
        const QString newPass = dlg.password();
        if (!newPass.isEmpty()) {
            m_password = newPass;
            savePassword(newPass);
        }
        loadSettings();
        loadNotifySettings();
        m_deckWindow->reload();
        rebuildTree();
        refresh();
    }
}

void MainWindow::showDeck()
{
    m_deckWindow->show();
    m_deckWindow->raise();
    m_deckWindow->activateWindow();
}

void MainWindow::showAbout()
{
    const QStringList lines = {
        QStringLiteral("nc-notify — Nextcloud Notify"),
        QStringLiteral("Версия: %1").arg(QApplication::applicationVersion()),
        QString(),
        QStringLiteral("Автор: Qwen (AI-ассистент, Alibaba Group)"),
        QStringLiteral("Идея, постановка задачи и тестирование: Andrew"),
        QString(),
        QStringLiteral("Назначение: настольное приложение для Astra Linux (Fly),"),
        QStringLiteral("синхронизируется с календарём Nextcloud по CalDAV и заранее"),
        QStringLiteral("напоминает о событиях."),
        QString(),
        QStringLiteral("Возможности текущей версии:"),
        QStringLiteral("- список событий: Сейчас / Сегодня / Завтра / Позже;"),
        QStringLiteral("- прогресс идущих событий и обратный отсчёт;"),
        QStringLiteral("- напоминания за произвольное число минут (список порогов);"),
        QStringLiteral("- каналы: своё окно и fly-notifications"),
        QStringLiteral("  (оба / только fly / только окно / тихо);"),
        QStringLiteral("- звуковой сигнал (libcanberra), включается и выключается;"),
        QStringLiteral("- пароль приложения в связке ключей gnome-keyring;"),
        QStringLiteral("- доверие самоподписанному сертификату по отпечатку SHA-256;"),
        QStringLiteral("- несколько календарей и авто-синхронизация по таймеру;"),
        QStringLiteral("- вход по паролю приложения или сквозной Kerberos (AD, экспериментально);"),
        QStringLiteral("- карточки Nextcloud Deck: доски, сроки, напоминания;"),
        QStringLiteral("- работа в трее: свернуть/развернуть, полный выход по меню."),
        QString(),
        QStringLiteral("Стек: C++17, Qt5, libical, QtKeychain, libcanberra.")
    };
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("О программе nc-notify"));
    box.setIconPixmap(QApplication::windowIcon().pixmap(64, 64));
    box.setTextFormat(Qt::PlainText);
    box.setText(lines.join(QLatin1Char('\n')));
    box.exec();
}

void MainWindow::setStatus(const QString &text, bool error)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? QStringLiteral("color:#b00020;") : QString());
}

void MainWindow::showFromTray()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_tray && m_tray->isVisible()) {
        event->ignore();
        hide();
        if (!m_trayHintShown) {
            m_trayHintShown = true;
            QSettings s;
            s.setValue(QStringLiteral("trayHintShown"), true);
            m_tray->showMessage(QStringLiteral("nc-notify"),
                QStringLiteral("Приложение свёрнуто в трей.\n"
                               "Пункт «Закрыть» в меню значка — полный выход."),
                QSystemTrayIcon::Information, 5000);
        }
    } else {
        event->accept();
    }
}
