#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QPair>
#include <QString>
#include <functional>

struct CurlRequest {
    QString url;
    QByteArray method;
    QByteArray body;
    QList<QPair<QByteArray, QByteArray>> headers;
    QString user;
    QString password;
    bool negotiate = false;
    QString caFile;
    int timeoutSec = 60;
};

struct CurlResponse {
    long status = 0;
    QByteArray body;
    QString error;
    bool certProblem = false;
    bool noCredentials = false;
};
Q_DECLARE_METATYPE(CurlResponse)

// Блокирующий libcurl-запрос в рабочем потоке, колбэк — в главном потоке.
class CurlTransport : public QObject
{
    Q_OBJECT
public:
    explicit CurlTransport(QObject *parent = nullptr);
    static bool available();      // есть ли GSS-API + SPNEGO
    static QString features();
    void send(const CurlRequest &req, std::function<void(const CurlResponse &)> cb);
};
