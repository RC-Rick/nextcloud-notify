#include "Autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

QString execLine()
{
    return QStringLiteral("\"%1\" --minimized")
        .arg(QCoreApplication::applicationFilePath());
}

QString iconLine()
{
    const QString sysIcon = QStringLiteral("/usr/share/icons/hicolor/64x64/apps/nc-notify.png");
    if (QFileInfo::exists(sysIcon))
        return QStringLiteral("nc-notify");
    const QString p = QCoreApplication::applicationDirPath()
                      + QStringLiteral("/../icons/nc-notify.png");
    if (QFileInfo::exists(p))
        return QFileInfo(p).absoluteFilePath();
    return QStringLiteral("x-office-calendar");
}

void write()
{
    const QString path = Autostart::desktopPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString content = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Nextcloud Notify\n"
        "Name[ru]=Nextcloud Notify\n"
        "Comment=Nextcloud calendar reminders\n"
        "Comment[ru]=Напоминания о событиях календаря Nextcloud\n"
        "Exec=%1\n"
        "Icon=%2\n"
        "Terminal=false\n"
        "Categories=Utility;Calendar;\n"
        "StartupNotify=false\n").arg(execLine(), iconLine());

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
    return QDir::homePath() + QStringLiteral("/.config/autostart/nc-notify.desktop");
}

bool Autostart::isEnabled()
{
    return QFile::exists(desktopPath());
}

void Autostart::setEnabled(bool on)
{
    if (on)
        write();
    else
        QFile::remove(desktopPath());
}

void Autostart::refresh()
{
    if (isEnabled())
        write();
}
