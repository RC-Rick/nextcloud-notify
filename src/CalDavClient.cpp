#include "CalDavClient.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTimer>
#include <QXmlStreamReader>

#include "CurlTransport.h"
#include "ICalParser.h"

static QByteArray utcStamp(const QDateTime &dt)
{
    return dt.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'")).toLatin1();
}

static void armTimeout(QNetworkReply *reply, int ms)
{
    QTimer::singleShot(ms, reply, [reply]() {
        if (reply->isRunning()) {
            reply->setProperty("timedOut", true);
            reply->abort();
        }
    });
}

CalDavClient::CalDavClient(QObject *parent)
    : QObject(parent),
      m_nam(new QNetworkAccessManager(this)),
      m_curl(new CurlTransport(this))
{
    connect(m_nam, &QNetworkAccessManager::sslErrors,
            this, &CalDavClient::onSslErrors);
}

void CalDavClient::setAccount(const QString &serverUrl, const QString &user, const QString &password)
{
    m_serverUrl = serverUrl;
    while (m_serverUrl.endsWith('/'))
        m_serverUrl.chop(1);
    m_user = user;
    m_password = password;
}

void CalDavClient::setAuthMode(int mode) { m_authMode = mode; }
void CalDavClient::setCaFile(const QString &path) { m_caFile = path; }
int CalDavClient::authMode() const { return m_authMode; }

bool CalDavClient::isConfigured() const
{
    if (m_serverUrl.isEmpty() || m_user.isEmpty())
        return false;
    return m_authMode == KerberosAuth || !m_password.isEmpty();
}

QString CalDavClient::calendarHome() const
{
    return m_serverUrl + QStringLiteral("/remote.php/dav/calendars/") + m_user + QLatin1Char('/');
}

QUrl CalDavClient::webCalendarUrl(const QDate &date) const
{
    return QUrl(m_serverUrl + QStringLiteral("/index.php/apps/calendar/dayGridMonth/")
                + date.toString(QStringLiteral("yyyy-MM-dd")));
}

QNetworkRequest CalDavClient::makeRequest(const QUrl &url) const
{
    QNetworkRequest req(url);
    req.setRawHeader("Authorization",
                     QByteArray("Basic ") + (m_user + ':' + m_password).toUtf8().toBase64());
    req.setRawHeader("User-Agent", "nc-notify/0.5");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

QString CalDavClient::httpError(QNetworkReply *reply, int code) const
{
    if (reply->property("sslDeclined").toBool())
        return QStringLiteral("Подключение отклонено: сертификат сервера не принят");
    if (reply->property("timedOut").toBool())
        return QStringLiteral("Таймаут: сервер не ответил за отведённое время");
    if (code == 401)
        return QStringLiteral("401: неверный логин или пароль приложения");
    if (code == 404)
        return QStringLiteral("404: адрес не найден — проверьте URL сервера и логин");
    QString msg = reply->errorString();
    if (code > 0)
        msg += QStringLiteral(" (HTTP %1)").arg(code);
    return msg;
}

QString CalDavClient::curlErrorText(const CurlResponse &r) const
{
    if (r.noCredentials)
        return QStringLiteral("Нет билета Kerberos (GSS-API). Войдите в доменную сессию "
                              "или выполните kinit в терминале, затем повторите.");
    if (r.certProblem)
        return QStringLiteral("Сертификат сервера не доверен системным хранилищем CA. "
                              "Укажите файл корневого сертификата домена в настройках "
                              "(Аккаунт → CA-сертификат) или добавьте его в систему.");
    if (r.status == 401)
        return QStringLiteral("401: сервер не принял сквозную авторизацию. Проверьте, что "
                              "Apache/Nginx настроен на SPNEGO для /remote.php/dav/.");
    if (r.status == 404)
        return QStringLiteral("404: адрес не найден — проверьте URL сервера и логин.");
    if (!r.error.isEmpty())
        return r.error;
    if (r.status > 0)
        return QStringLiteral("HTTP %1").arg(r.status);
    return QStringLiteral("Неизвестная ошибка запроса");
}

void CalDavClient::onSslErrors(QNetworkReply *reply, const QList<QSslError> &errors)
{
    const QSslCertificate cert = reply->sslConfiguration().peerCertificate();
    if (cert.isNull()) {
        reply->abort();
        return;
    }
    const QString digest =
        QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex(':'));
    QSettings s;
    const QString key = QStringLiteral("tls/pin/") + reply->url().host();
    const QString stored = s.value(key).toString();
    if (!stored.isEmpty() && stored == digest) {
        reply->ignoreSslErrors();
        return;
    }
    QStringList errTexts;
    for (const QSslError &e : errors)
        errTexts << e.errorString();
    const bool changed = !stored.isEmpty();
    const QString text = changed
        ? QStringLiteral("ВНИМАНИЕ: сертификат сервера ИЗМЕНИЛСЯ с прошлого подключения.\n"
                         "Это может быть признаком атаки. Доверять новому сертификату?")
        : QStringLiteral("Сервер использует сертификат, не подтверждённый системными центрами.\n"
                         "Доверять этому сертификату? (доверие по отпечатку SHA-256)");
    const QString details =
        QStringLiteral("Сервер: %1\nКому выдан: %2\nКем выдан: %3\nДействителен до: %4\n"
                       "SHA-256:\n%5\n\nОшибки:\n%6")
            .arg(reply->url().host(), cert.subjectDisplayName(), cert.issuerDisplayName(),
                 cert.expiryDate().toString(Qt::ISODate), digest, errTexts.join('\n'));
    QMessageBox box(qobject_cast<QWidget *>(parent()));
    box.setIcon(changed ? QMessageBox::Warning : QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("Сертификат сервера"));
    box.setText(text);
    box.setInformativeText(details);
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() == QMessageBox::Yes) {
        s.setValue(key, digest);
        reply->ignoreSslErrors();
    } else {
        reply->setProperty("sslDeclined", true);
        reply->abort();
    }
}

QByteArray CalDavClient::propfindBody() const
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<D:propfind xmlns:D=\"DAV:\"><D:prop>"
        "<D:resourcetype/><D:displayname/>"
        "</D:prop></D:propfind>").toUtf8();
}

QByteArray CalDavClient::reportBody(const QDateTime &from, const QDateTime &to) const
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<C:calendar-query xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">"
        "<D:prop><D:getetag/><C:calendar-data/></D:prop>"
        "<C:filter><C:comp-filter name=\"VCALENDAR\"><C:comp-filter name=\"VEVENT\">"
        "<C:time-range start=\"%1\" end=\"%2\"/>"
        "</C:comp-filter></C:comp-filter></C:filter></C:calendar-query>")
        .arg(QString::fromLatin1(utcStamp(from)), QString::fromLatin1(utcStamp(to)))
        .toUtf8();
}

QList<CalendarInfo> CalDavClient::parseCalendars(const QByteArray &data) const
{
    QList<CalendarInfo> out;
    QXmlStreamReader xml(data);
    const QString homePath = QUrl(calendarHome()).path();
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("multistatus")) { xml.skipCurrentElement(); continue; }
        while (xml.readNextStartElement()) {
            if (xml.name() != QLatin1String("response")) { xml.skipCurrentElement(); continue; }
            QString href, name;
            bool isCal = false;
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("href")) {
                    href = xml.readElementText();
                } else if (xml.name() == QLatin1String("propstat")) {
                    while (xml.readNextStartElement()) {
                        if (xml.name() != QLatin1String("prop")) { xml.skipCurrentElement(); continue; }
                        while (xml.readNextStartElement()) {
                            if (xml.name() == QLatin1String("displayname")) {
                                name = xml.readElementText();
                            } else if (xml.name() == QLatin1String("resourcetype")) {
                                while (xml.readNextStartElement()) {
                                    if (xml.name() == QLatin1String("calendar"))
                                        isCal = true;
                                    xml.skipCurrentElement();
                                }
                            } else {
                                xml.skipCurrentElement();
                            }
                        }
                    }
                } else {
                    xml.skipCurrentElement();
                }
            }
            if (!isCal || href.isEmpty())
                continue;
            QUrl u = QUrl(m_serverUrl).resolved(QUrl(href));
            QString path = u.path();
            if (path == homePath)
                continue;
            if (!path.endsWith('/'))
                path += '/';
            u.setPath(path);
            CalendarInfo ci;
            ci.url = u.toString();
            ci.name = name.isEmpty() ? path.section('/', -2, -2) : name;
            out.append(ci);
        }
    }
    return out;
}

QList<EventOccurrence> CalDavClient::parseEvents(const QByteArray &data, const QString &calName,
                                                 const QDateTime &from, const QDateTime &to) const
{
    QList<EventOccurrence> events;
    QXmlStreamReader xml(data);
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("multistatus")) { xml.skipCurrentElement(); continue; }
        while (xml.readNextStartElement()) {
            if (xml.name() != QLatin1String("response")) { xml.skipCurrentElement(); continue; }
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("propstat")) {
                    while (xml.readNextStartElement()) {
                        if (xml.name() != QLatin1String("prop")) { xml.skipCurrentElement(); continue; }
                        while (xml.readNextStartElement()) {
                            if (xml.name() == QLatin1String("calendar-data")) {
                                const QString ical = xml.readElementText();
                                events += ICalParser::parse(ical.toUtf8(), calName, from, to);
                            } else {
                                xml.skipCurrentElement();
                            }
                        }
                    }
                } else {
                    xml.skipCurrentElement();
                }
            }
        }
    }
    for (EventOccurrence &ev : events)
        ev.webUrl = webCalendarUrl(ev.start.date());
    return events;
}

void CalDavClient::discoverCalendars()
{
    if (!isConfigured()) {
        emit requestFailed(QStringLiteral("discover"), QStringLiteral("Аккаунт не настроен"));
        return;
    }
    if (m_authMode == KerberosAuth)
        discoverCurl();
    else
        discoverQt();
}

void CalDavClient::discoverQt()
{
    QNetworkRequest req = makeRequest(QUrl(calendarHome()));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");
    req.setRawHeader("Depth", "1");
    auto *buf = new QBuffer;
    buf->setData(propfindBody());
    buf->open(QIODevice::ReadOnly);
    QNetworkReply *reply = m_nam->sendCustomRequest(req, "PROPFIND", buf);
    buf->setParent(reply);
    armTimeout(reply, 30000);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(QStringLiteral("discover"), httpError(reply, code));
            return;
        }
        emit calendarsDiscovered(parseCalendars(reply->readAll()));
    });
}

void CalDavClient::discoverCurl()
{
    if (!CurlTransport::available()) {
        emit requestFailed(QStringLiteral("discover"),
                           QStringLiteral("libcurl собран без GSS-API/SPNEGO — сквозная "
                                          "авторизация недоступна. Используйте вход по паролю."));
        return;
    }
    CurlRequest r;
    r.url = calendarHome();
    r.method = "PROPFIND";
    r.body = propfindBody();
    r.headers.append(qMakePair(QByteArray("Content-Type"),
                               QByteArray("application/xml; charset=utf-8")));
    r.headers.append(qMakePair(QByteArray("Depth"), QByteArray("1")));
    r.negotiate = true;
    r.caFile = m_caFile;
    r.timeoutSec = 30;
    m_curl->send(r, [this](const CurlResponse &resp) {
        if (!resp.error.isEmpty() || resp.status < 200 || resp.status >= 300) {
            emit requestFailed(QStringLiteral("discover"), curlErrorText(resp));
            return;
        }
        emit calendarsDiscovered(parseCalendars(resp.body));
    });
}

void CalDavClient::fetchEvents(const CalendarInfo &cal, const QDateTime &from, const QDateTime &to)
{
    if (!isConfigured()) {
        emit requestFailed(cal.url, QStringLiteral("Аккаунт не настроен"));
        return;
    }
    if (m_authMode == KerberosAuth)
        fetchCurl(cal, from, to);
    else
        fetchQt(cal, from, to);
}

void CalDavClient::fetchQt(const CalendarInfo &cal, const QDateTime &from, const QDateTime &to)
{
    QNetworkRequest req = makeRequest(QUrl(cal.url));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");
    req.setRawHeader("Depth", "1");
    auto *buf = new QBuffer;
    buf->setData(reportBody(from, to));
    buf->open(QIODevice::ReadOnly);
    QNetworkReply *reply = m_nam->sendCustomRequest(req, "REPORT", buf);
    buf->setParent(reply);
    armTimeout(reply, 60000);
    reply->setProperty("calUrl", cal.url);
    reply->setProperty("calName", cal.name);
    reply->setProperty("from", from);
    reply->setProperty("to", to);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString calUrl = reply->property("calUrl").toString();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(calUrl, httpError(reply, code));
            return;
        }
        emit eventsFetched(calUrl,
                           parseEvents(reply->readAll(),
                                       reply->property("calName").toString(),
                                       reply->property("from").toDateTime(),
                                       reply->property("to").toDateTime()));
    });
}

void CalDavClient::fetchCurl(const CalendarInfo &cal, const QDateTime &from, const QDateTime &to)
{
    if (!CurlTransport::available()) {
        emit requestFailed(cal.url,
                           QStringLiteral("libcurl без GSS-API/SPNEGO — сквозная авторизация "
                                          "недоступна."));
        return;
    }
    CurlRequest r;
    r.url = cal.url;
    r.method = "REPORT";
    r.body = reportBody(from, to);
    r.headers.append(qMakePair(QByteArray("Content-Type"),
                               QByteArray("application/xml; charset=utf-8")));
    r.headers.append(qMakePair(QByteArray("Depth"), QByteArray("1")));
    r.negotiate = true;
    r.caFile = m_caFile;
    r.timeoutSec = 60;
    const QString calUrl = cal.url;
    const QString calName = cal.name;
    m_curl->send(r, [this, calUrl, calName, from, to](const CurlResponse &resp) {
        if (!resp.error.isEmpty() || resp.status < 200 || resp.status >= 300) {
            emit requestFailed(calUrl, curlErrorText(resp));
            return;
        }
        emit eventsFetched(calUrl, parseEvents(resp.body, calName, from, to));
    });
}
