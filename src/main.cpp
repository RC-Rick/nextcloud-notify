#include <QApplication>
#include <cstdio>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QSettings>
#include <QSystemTrayIcon>

#include "Autostart.h"
#include "CurlTransport.h"
#include "MainWindow.h"
#include "TrayController.h"

static void logHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    Q_UNUSED(ctx);
    const bool verbose = qEnvironmentVariableIsSet("NC_NOTIFY_DEBUG");
    if ((type == QtDebugMsg || type == QtInfoMsg) && !verbose)
        return;
    const char *prefix = "";
    switch (type) {
    case QtDebugMsg:    prefix = "debug: ";   break;
    case QtInfoMsg:     prefix = "";          break;
    case QtWarningMsg:  prefix = "warning: "; break;
    case QtCriticalMsg: prefix = "error: ";   break;
    case QtFatalMsg:    prefix = "fatal: ";   break;
    }
    fprintf(stderr, "%s%s\n", prefix, qPrintable(msg));
    fflush(stderr);
    if (type == QtFatalMsg)
        abort();
}

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        if (a == "--verbose" || a == "--curl-info")
            qputenv("NC_NOTIFY_DEBUG", "1");
    }
    qInstallMessageHandler(logHandler);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("nc-notify"));
    QApplication::setOrganizationName(QStringLiteral("nc-notify"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.1"));

    if (app.arguments().contains(QStringLiteral("--version"))) {
        printf("%s\n", qPrintable(QApplication::applicationVersion()));
        return 0;
    }
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/nc-notify.png")));
    QApplication::setQuitOnLastWindowClosed(false);

    if (app.arguments().contains(QStringLiteral("--curl-info"))) {
        qInfo("nc-notify: %s", qPrintable(CurlTransport::features()));
        qInfo("nc-notify: Negotiate доступен: %s", CurlTransport::available() ? "да" : "нет");
        return 0;
    }

    // Single-instance: если копия уже работает — просим её показать окно и выходим.
    const QString socketName = QStringLiteral("nc-notify-%1")
                                   .arg(qEnvironmentVariable("USER", QStringLiteral("user")));
    QLocalSocket probe;
    probe.connectToServer(socketName);
    if (probe.waitForConnected(300)) {
        const bool secondMinimized = app.arguments().contains(QStringLiteral("--minimized"));
        probe.write(secondMinimized ? "ping\n" : "show\n");
        probe.flush();
        probe.waitForBytesWritten(300);
        probe.disconnectFromServer();
        return 0;
    }
    QLocalServer::removeServer(socketName); // убрать осиротевший сокет прошлой копии
    QLocalServer server;
    if (!server.listen(socketName))
        qWarning("nc-notify: single-instance server not started: %s",
                 qPrintable(server.errorString()));

    MainWindow window;
    TrayController tray(&window);
    window.setTrayIcon(tray.icon());

    if (!tray.isAvailable()) {
        QMessageBox::warning(&window, QStringLiteral("nc-notify"),
            QStringLiteral("Системный трей недоступен. "
                           "Приложение будет работать в обычном окне."));
        QApplication::setQuitOnLastWindowClosed(true);
    }

    QObject::connect(&server, &QLocalServer::newConnection, [&window, &server]() {
        QLocalSocket *c = server.nextPendingConnection();
        if (!c)
            return;
        c->waitForReadyRead(200);
        const QByteArray cmd = c->readAll();
        c->deleteLater();
        if (cmd.contains("show"))
            window.showFromTray();
    });

    const bool flagMinimized = app.arguments().contains(QStringLiteral("--minimized"));
    const bool settingMinimized =
        QSettings().value(QStringLiteral("startup/minimized"), false).toBool();
    const bool minimized = flagMinimized || settingMinimized;
    qInfo("nc-notify: startup minimized=%d (flag=%d, setting=%d)",
          int(minimized), int(flagMinimized), int(settingMinimized));
    if (!minimized)
        window.show();

    if (Autostart::isEnabled())
        Autostart::refresh(); // самообновление пути бинарника в .desktop

    return app.exec();
}
