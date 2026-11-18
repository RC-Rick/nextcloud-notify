#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSslError>
#include <QUrl>

#include "Event.h"

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class CurlTransport;
struct CurlResponse;

class CalDavClient : public QObject
{
    Q_OBJECT
public:
    enum AuthMode { PasswordAuth = 0, KerberosAuth = 1 };

    explicit CalDavClient(QObject *parent = nullptr);
    void setAccount(const QString &serverUrl, const QString &user, const QString &password);
    void setAuthMode(int mode);
    void setCaFile(const QString &path);
    int authMode() const;
    bool isConfigured() const;
    QString calendarHome() const;
    QUrl webCalendarUrl(const QDate &date) const;
    void discoverCalendars();
    void fetchEvents(const CalendarInfo &cal, const QDateTime &from, const QDateTime &to);

signals:
    void calendarsDiscovered(const QList<CalendarInfo> &calendars);
    void eventsFetched(const QString &calendarUrl, const QList<EventOccurrence> &events);
    void requestFailed(const QString &context, const QString &message);

private slots:
    void onSslErrors(QNetworkReply *reply, const QList<QSslError> &errors);

private:
    QNetworkRequest makeRequest(const QUrl &url) const;
    QString httpError(QNetworkReply *reply, int code) const;
    QString curlErrorText(const CurlResponse &r) const;
    QByteArray propfindBody() const;
    QByteArray reportBody(const QDateTime &from, const QDateTime &to) const;
    QList<CalendarInfo> parseCalendars(const QByteArray &data) const;
    QList<EventOccurrence> parseEvents(const QByteArray &data, const QString &calName,
                                       const QDateTime &from, const QDateTime &to) const;
    void discoverQt();
    void discoverCurl();
    void fetchQt(const CalendarInfo &cal, const QDateTime &from, const QDateTime &to);
    void fetchCurl(const CalendarInfo &cal, const QDateTime &from, const QDateTime &to);

    QNetworkAccessManager *m_nam = nullptr;
    CurlTransport *m_curl = nullptr;
    QString m_serverUrl, m_user, m_password, m_caFile;
    int m_authMode = PasswordAuth;
};
