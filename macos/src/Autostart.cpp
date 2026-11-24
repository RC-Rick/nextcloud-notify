#include "Autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

// macOS: автозапуск через LaunchAgent (~/Library/LaunchAgents).
// Имя файла и сигнатура Autostart совпадают с Linux-версией, чтобы
// SettingsDialog и main.cpp собирались без правок.

namespace {

const QString kLabel = QStringLiteral("com.nc-notify.agent");

QString xmlEscape(QString s)
{
    s.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    s.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    s.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return s;
}

QString executablePath()
{
    return QCoreApplication::applicationFilePath();
}

void writePlist()
{
    const QString path = Autostart::desktopPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    const QString content = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "    <key>Label</key>\n"
        "    <string>%1</string>\n"
        "    <key>ProgramArguments</key>\n"
        "    <array>\n"
        "        <string>%2</string>\n"
        "        <string>--minimized</string>\n"
        "    </array>\n"
        "    <key>RunAtLoad</key>\n"
        "    <true/>\n"
        "    <key>KeepAlive</key>\n"
        "    <false/>\n"
        "    <key>ProcessType</key>\n"
        "    <string>Interactive</string>\n"
        "    <key>LimitLoadToSessionType</key>\n"
        "    <string>Aqua</string>\n"
        "</dict>\n"
        "</plist>\n")
        .arg(kLabel, xmlEscape(executablePath()));

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(content.toUtf8());
    f.flush();
    f.close();
}

} // namespace

QString Autostart::desktopPath()
{
    return QDir::homePath()
           + QStringLiteral("/Library/LaunchAgents/") + kLabel + QStringLiteral(".plist");
}

bool Autostart::isEnabled()
{
    return QFile::exists(desktopPath());
}

void Autostart::setEnabled(bool on)
{
    if (on)
        writePlist();
    else
        QFile::remove(desktopPath());
}

void Autostart::refresh()
{
    if (isEnabled())
        writePlist();
}
