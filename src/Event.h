#pragma once

#include <QDateTime>
#include <QString>
#include <QUrl>

struct CalendarInfo {
    QString url;   // абсолютный URL коллекции, со слэшем на конце
    QString name;
};

struct EventOccurrence {
    QString uid;
    QString summary;
    QString location;
    QString description;
    QString calendarName;
    QDateTime start;   // локальное время
    QDateTime end;     // локальное время
    bool allDay = false;
    QUrl webUrl;
};

inline QString encodeCalendar(const CalendarInfo &c)
{
    return c.url + QChar(0x1f) + c.name;
}

inline CalendarInfo decodeCalendar(const QString &s)
{
    const int i = s.indexOf(QChar(0x1f));
    if (i < 0)
        return CalendarInfo{s, QString()};
    return CalendarInfo{s.left(i), s.mid(i + 1)};
}
