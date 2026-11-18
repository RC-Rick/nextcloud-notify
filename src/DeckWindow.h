#pragma once

#include <QList>
#include <QWidget>

#include "DeckClient.h"

class QTreeWidget;
class QLabel;
class QTimer;
class NotificationManager;

class DeckWindow : public QWidget
{
    Q_OBJECT
public:
    explicit DeckWindow(NotificationManager *nm, QWidget *parent = nullptr);
    void reload();

public slots:
    void refresh();

protected:
    void showEvent(QShowEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private slots:
    void onCards(int boardId, const QList<DeckCard> &cards);
    void onError(const QString &ctx, const QString &msg);

private:
    void loadPassword();
    void finish();
    void rebuild();
    void setStatus(const QString &text, bool error = false);

    DeckClient *m_client = nullptr;
    NotificationManager *m_notify = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_status = nullptr;
    QTimer *m_timer = nullptr;
    QString m_url, m_user, m_password;
    bool m_enabled = false;
    bool m_showNoDue = false;
    QList<DeckBoard> m_boards;
    QList<DeckCard> m_cards;
    int m_pending = 0;
    bool m_syncing = false;
    QString m_lastError;
};
