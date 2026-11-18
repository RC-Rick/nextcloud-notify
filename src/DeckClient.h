#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSslError>
#include <QString>
#include <QUrl>

struct DeckBoard {
    int id = 0;
    QString title;
};

struct DeckCard {
    int id = 0;
    int boardId = 0;
    QString title;
    QString boardTitle;
    QString stackTitle;
    QDateTime due;
    bool hasDue = false;
    bool done = false;
    QString description;
    QUrl url;
};

class QNetworkAccessManager;
class QNetworkReply;

class DeckClient : public QObject
{
    Q_OBJECT
public:
    explicit DeckClient(QObject *parent = nullptr);
    void setAccount(const QString &serverUrl, const QString &user, const QString &password);
    bool isConfigured() const;
    void fetchBoards();
    void fetchCards(const DeckBoard &board);

signals:
    void boardsReady(const QList<DeckBoard> &boards);
    void cardsReady(int boardId, const QList<DeckCard> &cards);
    void failed(const QString &context, const QString &message);

private slots:
    void onSslErrors(QNetworkReply *reply, const QList<QSslError> &errors);

private:
    QNetworkReply *get(const QString &path);
    QString errorText(QNetworkReply *reply) const;

    QNetworkAccessManager *m_nam = nullptr;
    QString m_url, m_user, m_password;
};
