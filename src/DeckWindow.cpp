#include "DeckWindow.h"

#include <QBrush>
#include <QCloseEvent>
#include <QColor>
#include <QDesktopServices>
#include <QFont>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <qt5keychain/keychain.h>

#include <algorithm>

#include "Event.h"
#include "NotificationManager.h"

namespace {

QString durationText(qint64 secs)
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

} // namespace

DeckWindow::DeckWindow(NotificationManager *nm, QWidget *parent)
    : QWidget(parent), m_notify(nm)
{
    setWindowFlags(Qt::Window);
    setWindowTitle(QStringLiteral("Карточки Deck"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/nc-notify.png")));
    resize(760, 520);

    auto *lay = new QVBoxLayout(this);
    auto *row = new QHBoxLayout;
    auto *refreshBtn = new QPushButton(QStringLiteral("Обновить"), this);
    connect(refreshBtn, &QPushButton::clicked, this, &DeckWindow::refresh);
    row->addWidget(refreshBtn);
    row->addStretch();
    row->addWidget(new QLabel(QStringLiteral("Доски настраиваются: Настройки → Deck"), this));
    lay->addLayout(row);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({QStringLiteral("Срок"), QStringLiteral("Карточка"),
                             QStringLiteral("Доска / колонка"), QStringLiteral("Осталось")});
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->setColumnWidth(0, 120);
    m_tree->setColumnWidth(2, 210);
    m_tree->setColumnWidth(3, 110);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it, int) {
        if (!it || !it->parent())
            return;
        const QUrl u = it->data(0, Qt::UserRole).toUrl();
        if (u.isValid())
            QDesktopServices::openUrl(u);
    });
    lay->addWidget(m_tree, 1);

    m_status = new QLabel(this);
    lay->addWidget(m_status);

    m_client = new DeckClient(this);
    connect(m_client, &DeckClient::cardsReady, this, &DeckWindow::onCards);
    connect(m_client, &DeckClient::failed, this, &DeckWindow::onError);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &DeckWindow::refresh);

    loadPassword();
    reload();
    m_timer->start();
}

void DeckWindow::loadPassword()
{
    auto *job = new QKeychain::ReadPasswordJob(QStringLiteral("nc-notify"), this);
    job->setAutoDelete(false);
    job->setKey(QStringLiteral("caldavPassword"));
    connect(job, &QKeychain::Job::finished, this, [this, job]() {
        if (job->error() == QKeychain::NoError)
            m_password = job->textData();
        job->deleteLater();
        m_client->setAccount(m_url, m_user, m_password);
        if (m_enabled)
            refresh();
    });
    job->start();
}

void DeckWindow::reload()
{
    QSettings s;
    m_enabled = s.value(QStringLiteral("deck/enabled"), false).toBool();
    m_showNoDue = s.value(QStringLiteral("deck/showNoDue"), false).toBool();
    m_url = s.value(QStringLiteral("account/serverUrl")).toString();
    while (m_url.endsWith('/'))
        m_url.chop(1);
    m_user = s.value(QStringLiteral("account/user")).toString();
    m_boards.clear();
    const QStringList entries = s.value(QStringLiteral("deck/boards")).toStringList();
    for (const QString &e : entries) {
        DeckBoard b;
        b.id = e.section('|', 0, 0).toInt();
        b.title = e.section('|', 1);
        if (b.id > 0)
            m_boards.append(b);
    }
    m_timer->setInterval(s.value(QStringLiteral("sync/intervalMin"), 5).toInt() * 60000);
    m_client->setAccount(m_url, m_user, m_password);
    rebuild();
    if (m_enabled)
        refresh();
    else
        setStatus(QStringLiteral("Карточки Deck отключены (Настройки → Deck)"));
}

void DeckWindow::refresh()
{
    if (m_syncing)
        return;
    if (!m_enabled) {
        m_cards.clear();
        rebuild();
        setStatus(QStringLiteral("Карточки Deck отключены (Настройки → Deck)"));
        return;
    }
    if (m_url.isEmpty() || m_user.isEmpty() || m_password.isEmpty()) {
        setStatus(QStringLiteral("Укажите аккаунт в «Настройках»"), true);
        return;
    }
    if (m_boards.isEmpty()) {
        setStatus(QStringLiteral("Доски не выбраны (Настройки → Deck)"), true);
        return;
    }
    m_syncing = true;
    m_lastError.clear();
    m_cards.clear();
    m_pending = m_boards.count();
    setStatus(QStringLiteral("Синхронизация Deck…"));
    for (const DeckBoard &b : m_boards)
        m_client->fetchCards(b);
}

void DeckWindow::onCards(int boardId, const QList<DeckCard> &cards)
{
    Q_UNUSED(boardId);
    m_cards += cards;
    if (m_pending > 0)
        --m_pending;
    if (m_pending == 0 && m_syncing)
        finish();
}

void DeckWindow::onError(const QString &ctx, const QString &msg)
{
    Q_UNUSED(ctx);
    m_lastError = msg;
    if (m_pending > 0)
        --m_pending;
    if (m_pending == 0 && m_syncing)
        finish();
}

void DeckWindow::finish()
{
    m_syncing = false;
    std::sort(m_cards.begin(), m_cards.end(), [](const DeckCard &a, const DeckCard &b) {
        if (a.hasDue != b.hasDue)
            return a.hasDue;
        if (a.hasDue && b.hasDue && a.due != b.due)
            return a.due < b.due;
        return a.title < b.title;
    });
    rebuild();

    if (m_notify) {
        QList<EventOccurrence> due;
        const QDateTime now = QDateTime::currentDateTime();
        for (const DeckCard &c : m_cards) {
            if (!c.hasDue || c.done || c.due <= now)
                continue;
            EventOccurrence ev;
            ev.uid = QStringLiteral("deck-") + QString::number(c.id);
            ev.summary = c.title;
            ev.location = c.boardTitle + QStringLiteral(" · ") + c.stackTitle;
            ev.calendarName = QStringLiteral("Deck");
            ev.description = c.description;
            ev.start = c.due;
            ev.end = c.due;
            ev.allDay = false;
            ev.webUrl = c.url;
            due.append(ev);
        }
        m_notify->check(due);
    }

    if (m_lastError.isEmpty())
        setStatus(QStringLiteral("Обновлено %1 · карточек: %2")
                      .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")))
                      .arg(m_cards.count()));
    else
        setStatus(QStringLiteral("Ошибка: %1").arg(m_lastError), true);
}

void DeckWindow::rebuild()
{
    m_tree->clear();
    const QDateTime now = QDateTime::currentDateTime();
    const QDate today = now.date();
    QHash<QString, QTreeWidgetItem *> groups;
    auto group = [this, &groups](const QString &title) -> QTreeWidgetItem * {
        auto it = groups.find(title);
        if (it != groups.end())
            return it.value();
        auto *g = new QTreeWidgetItem(m_tree);
        g->setText(0, title);
        QFont f = g->font(0);
        f.setBold(true);
        g->setFont(0, f);
        g->setFirstColumnSpanned(true);
        groups.insert(title, g);
        return g;
    };

    for (const DeckCard &c : m_cards) {
        QString gname;
        if (!c.hasDue) {
            if (!m_showNoDue)
                continue;
            gname = QStringLiteral("Без срока");
        } else if (c.due < now) {
            gname = QStringLiteral("Просрочено");
        } else if (c.due.date() == today) {
            gname = QStringLiteral("Сегодня");
        } else if (c.due.date() == today.addDays(1)) {
            gname = QStringLiteral("Завтра");
        } else if (c.due.date() <= today.addDays(7)) {
            gname = QStringLiteral("На неделе");
        } else {
            gname = QStringLiteral("Позже");
        }
        auto *it = new QTreeWidgetItem(group(gname));
        it->setText(0, c.hasDue ? c.due.toString(QStringLiteral("dd.MM HH:mm"))
                                : QStringLiteral("—"));
        it->setText(1, c.title);
        it->setText(2, c.boardTitle + QStringLiteral(" · ") + c.stackTitle);
        if (c.hasDue) {
            const qint64 secs = now.secsTo(c.due);
            it->setText(3, secs < 0 ? QStringLiteral("просрочено") : durationText(secs));
        }
        it->setData(0, Qt::UserRole, c.url);
        it->setToolTip(1, c.description.left(500));
        if (c.hasDue && c.due < now) {
            const QBrush red(QColor(0xb0, 0x00, 0x20));
            for (int col = 0; col < 4; ++col)
                it->setForeground(col, red);
        }
    }
    if (m_tree->topLevelItemCount() == 0) {
        auto *it = new QTreeWidgetItem(m_tree);
        it->setText(0, m_enabled ? QStringLiteral("Карточек нет (или доски не выбраны)")
                                 : QStringLiteral("Карточки Deck отключены — включите в Настройках → Deck"));
        it->setFirstColumnSpanned(true);
    }
    m_tree->expandAll();
}

void DeckWindow::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(error ? QStringLiteral("color:#b00020;") : QString());
}

void DeckWindow::showEvent(QShowEvent *e)
{
    reload();
    QWidget::showEvent(e);
}

void DeckWindow::closeEvent(QCloseEvent *e)
{
    hide();
    e->ignore();
}
