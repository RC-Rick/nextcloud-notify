#include "DeckClient.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTimer>

namespace {

void armTimeout(QNetworkReply *r, int ms)
{
    QTimer::singleShot(ms, r, [r]() {
        if (r->isRunning()) {
            r->setProperty("timedOut", true);
            r->abort();
        }
    });
}

QJsonArray jsonArray(const QByteArray &data)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isArray())
        return doc.array();
    if (doc.isObject()) {
        const QJsonObject o = doc.object();
        const QJsonValue d = o.value(QStringLiteral("ocs")).toObject().value(QStringLiteral("data"));
        if (d.isArray())
            return d.toArray();
        if (d.isObject())
            return QJsonArray{d.toObject()};
        if (o.value(QStringLiteral("data")).isArray())
            return o.value(QStringLiteral("data")).toArray();
    }
    return QJsonArray();
}

QDateTime parseDue(const QString &s)
{
    if (s.isEmpty())
        return QDateTime();
    QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
    if (!dt.isValid())
        dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    return dt.isValid() ? dt.toLocalTime() : QDateTime();
}

} // namespace

DeckClient::DeckClient(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
    connect(m_nam, &QNetworkAccessManager::sslErrors, this, &DeckClient::onSslErrors);
}

void DeckClient::setAccount(const QString &serverUrl, const QString &user, const QString &password)
{
    m_url = serverUrl;
    while (m_url.endsWith('/'))
        m_url.chop(1);
    m_user = user;
    m_password = password;
}

bool DeckClient::isConfigured() const
{
    return !m_url.isEmpty() && !m_user.isEmpty() && !m_password.isEmpty();
}

QNetworkReply *DeckClient::get(const QString &path)
{
    QNetworkRequest req(QUrl(m_url + path));
    req.setRawHeader("Authorization",
                     QByteArray("Basic ") + (m_user + ':' + m_password).toUtf8().toBase64());
    req.setRawHeader("OCS-APIRequest", "true");
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", "nc-notify/0.6");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *r = m_nam->get(req);
    armTimeout(r, 30000);
    return r;
}

void DeckClient::onSslErrors(QNetworkReply *reply, const QList<QSslError> &errors)
{
    Q_UNUSED(errors);
    const QSslCertificate cert = reply->sslConfiguration().peerCertificate();
    QSettings s;
    const QString pin = s.value(QStringLiteral("tls/pin/") + reply->url().host()).toString();
    const QString digest = QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex(':'));
    if (!cert.isNull() && !pin.isEmpty() && pin == digest) {
        reply->ignoreSslErrors();
        return;
    }
    reply->setProperty("pinMissing", true);
    reply->abort();
}

QString DeckClient::errorText(QNetworkReply *reply) const
{
    if (reply->property("pinMissing").toBool())
        return QStringLiteral("Сертификат сервера не подтверждён. Откройте Настройки → Аккаунт → "
                              "«Найти календари» и примите сертификат, затем повторите.");
    if (reply->property("timedOut").toBool())
        return QStringLiteral("Таймаут запроса Deck.");
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (code == 401)
        return QStringLiteral("401: неверный логин или пароль приложения.");
    if (code == 404)
        return QStringLiteral("404: Deck API не найден — проверьте, что приложение Deck установлено.");
    QString msg = reply->errorString();
    if (code > 0)
        msg += QStringLiteral(" (HTTP %1)").arg(code);
    return msg;
}

void DeckClient::fetchBoards()
{
    if (!isConfigured()) {
        emit failed(QStringLiteral("boards"), QStringLiteral("Аккаунт не настроен"));
        return;
    }
    QNetworkReply *r = get(QStringLiteral("/index.php/apps/deck/api/v1.0/boards"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            emit failed(QStringLiteral("boards"), errorText(r));
            return;
        }
        QList<DeckBoard> out;
        const QJsonArray arr = jsonArray(r->readAll());
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("archived")).toBool()
                || o.value(QStringLiteral("deletedAt")).toInt() > 0)
                continue;
            DeckBoard b;
            b.id = o.value(QStringLiteral("id")).toInt();
            b.title = o.value(QStringLiteral("title")).toString();
            if (b.id > 0)
                out.append(b);
        }
        emit boardsReady(out);
    });
}

void DeckClient::fetchCards(const DeckBoard &board)
{
    if (!isConfigured()) {
        emit failed(QStringLiteral("cards"), QStringLiteral("Аккаунт не настроен"));
        return;
    }
    const QString path = QStringLiteral("/index.php/apps/deck/api/v1.0/boards/%1/stacks").arg(board.id);
    QNetworkReply *r = get(path);
    const int boardId = board.id;
    const QString boardTitle = board.title;
    connect(r, &QNetworkReply::finished, this, [this, r, boardId, boardTitle]() {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            emit failed(QStringLiteral("cards"), errorText(r));
            return;
        }
        QList<DeckCard> out;
        const QJsonArray stacks = jsonArray(r->readAll());
        for (const QJsonValue &sv : stacks) {
            const QJsonObject stack = sv.toObject();
            const QString stackTitle = stack.value(QStringLiteral("title")).toString();
            const QJsonArray cards = stack.value(QStringLiteral("cards")).toArray();
            for (const QJsonValue &cv : cards) {
                const QJsonObject o = cv.toObject();
                if (o.value(QStringLiteral("archived")).toBool()
                    || o.value(QStringLiteral("deletedAt")).toInt() > 0)
                    continue;
                DeckCard c;
                c.id = o.value(QStringLiteral("id")).toInt();
                c.boardId = boardId;
                c.boardTitle = boardTitle;
                c.stackTitle = stackTitle;
                c.title = o.value(QStringLiteral("title")).toString();
                c.description = o.value(QStringLiteral("description")).toString();
                const QJsonValue doneVal = o.value(QStringLiteral("done"));
                c.done = !doneVal.isNull() && !doneVal.toString().isEmpty();
                if (c.done)
                    continue;
                c.due = parseDue(o.value(QStringLiteral("duedate")).toString());
                c.hasDue = c.due.isValid();
                c.url = QUrl(m_url + QStringLiteral("/index.php/apps/deck/#/board/%1/card/%2")
                                 .arg(boardId).arg(c.id));
                if (c.id > 0)
                    out.append(c);
            }
        }
        emit cardsReady(boardId, out);
    });
}
