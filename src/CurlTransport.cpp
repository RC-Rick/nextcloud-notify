#include "CurlTransport.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <curl/curl.h>

namespace {

size_t writeCb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *buf = static_cast<QByteArray *>(userdata);
    const size_t total = size * nmemb;
    buf->append(ptr, static_cast<int>(total));
    return total;
}

CurlResponse performCurl(const CurlRequest &req)
{
    CurlResponse resp;
    CURL *h = curl_easy_init();
    if (!h) {
        resp.error = QStringLiteral("Не удалось инициализировать curl");
        return resp;
    }

    const QByteArray url = req.url.toUtf8();
    const QByteArray method = req.method;
    const QByteArray userpwd = req.negotiate
                                   ? QByteArray(":")
                                   : (req.user + ':' + req.password).toUtf8();
    const QByteArray ca = req.caFile.toUtf8();
    char errbuf[CURL_ERROR_SIZE] = {0};

    curl_slist *hdrs = nullptr;
    for (const auto &pair : req.headers)
        hdrs = curl_slist_append(hdrs, (pair.first + ": " + pair.second).constData());

    curl_easy_setopt(h, CURLOPT_URL, url.constData());
    curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, method.constData());
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, long(req.timeoutSec));
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "nc-notify/0.5");
    curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");

    if (!req.body.isEmpty()) {
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(req.body.size()));
        curl_easy_setopt(h, CURLOPT_COPYPOSTFIELDS, req.body.constData());
    }
    if (req.negotiate) {
        curl_easy_setopt(h, CURLOPT_HTTPAUTH, long(CURLAUTH_NEGOTIATE));
        curl_easy_setopt(h, CURLOPT_USERPWD, userpwd.constData());
    } else if (!req.user.isEmpty()) {
        curl_easy_setopt(h, CURLOPT_HTTPAUTH, long(CURLAUTH_BASIC));
        curl_easy_setopt(h, CURLOPT_USERPWD, userpwd.constData());
    }
    if (!ca.isEmpty())
        curl_easy_setopt(h, CURLOPT_CAINFO, ca.constData());
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 2L);

    const CURLcode rc = curl_easy_perform(h);
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &resp.status);
    if (rc != CURLE_OK) {
        resp.error = QString::fromUtf8(errbuf[0] ? errbuf : curl_easy_strerror(rc));
        resp.certProblem = (rc == CURLE_PEER_FAILED_VERIFICATION
                            || rc == CURLE_SSL_ISSUER_ERROR
                            || rc == CURLE_SSL_CERTPROBLEM
                            || rc == CURLE_SSL_CACERT_BADFILE);
        const QString e = resp.error.toLower();
        resp.noCredentials = e.contains(QLatin1String("credential"))
                             || e.contains(QLatin1String("gss_init"))
                             || e.contains(QLatin1String("kerberos"))
                             || e.contains(QLatin1String("spnego"));
    }

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(h);
    return resp;
}

} // namespace

CurlTransport::CurlTransport(QObject *parent)
    : QObject(parent)
{
    static bool inited = false;
    if (!inited) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        inited = true;
    }
}

bool CurlTransport::available()
{
    const curl_version_info_data *v = curl_version_info(CURLVERSION_NOW);
    return v && (v->features & CURL_VERSION_GSSAPI) && (v->features & CURL_VERSION_SPNEGO);
}

QString CurlTransport::features()
{
    const curl_version_info_data *v = curl_version_info(CURLVERSION_NOW);
    if (!v)
        return QStringLiteral("libcurl: нет данных");
    return QStringLiteral("libcurl %1 | SSL: %2 | GSS-API: %3 | SPNEGO: %4")
        .arg(QString::fromUtf8(v->version),
             v->ssl_version ? QString::fromUtf8(v->ssl_version) : QStringLiteral("нет"),
             (v->features & CURL_VERSION_GSSAPI) ? QStringLiteral("да") : QStringLiteral("нет"),
             (v->features & CURL_VERSION_SPNEGO) ? QStringLiteral("да") : QStringLiteral("нет"));
}

void CurlTransport::send(const CurlRequest &req, std::function<void(const CurlResponse &)> cb)
{
    auto *w = new QFutureWatcher<CurlResponse>(this);
    connect(w, &QFutureWatcher<CurlResponse>::finished, this, [w, cb]() {
        const CurlResponse r = w->result();
        w->deleteLater();
        if (cb)
            cb(r);
    });
    w->setFuture(QtConcurrent::run([req]() { return performCurl(req); }));
}
