#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>

#include "Event.h"

namespace ICalParser {
// Разворачивает VEVENT (включая RRULE/RDATE/EXDATE и переопределения RECURRENCE-ID)
// в список вхождений, пересекающих окно [from, to).
QList<EventOccurrence> parse(const QByteArray &data, const QString &calendarName,
                             const QDateTime &from, const QDateTime &to);
}
