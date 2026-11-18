#include "ICalParser.h"

#include <QHash>
#include <QSet>
#include <libical/ical.h>

namespace {

QString qs(const char *s) { return s ? QString::fromUtf8(s) : QString(); }

QDateTime toQt(const icaltimetype &t)
{
    if (icaltime_is_null_time(t))
        return QDateTime();
    const QDate d(t.year, t.month, t.day);
    if (icaltime_is_date(t))
        return QDateTime(d, QTime(0, 0), Qt::LocalTime);
    const QTime tm(t.hour, t.minute, t.second);
    if (icaltime_is_utc(t))
        return QDateTime(d, tm, Qt::UTC).toLocalTime();
    const icaltimezone *zone = icaltime_get_timezone(t);
    if (!zone) {
        const char *tzid = icaltime_get_tzid(t);
        if (tzid)
            zone = icaltimezone_get_builtin_timezone(tzid);
    }
    if (zone) {
        const icaltimetype u = icaltime_convert_to_zone(t, icaltimezone_get_utc_timezone());
        return QDateTime(QDate(u.year, u.month, u.day),
                         QTime(u.hour, u.minute, u.second), Qt::UTC).toLocalTime();
    }
    return QDateTime(d, tm, Qt::LocalTime); // floating — считаем локальным
}

icaltimetype fromQtUtc(const QDateTime &dt)
{
    return icaltime_from_timet_with_zone(
        static_cast<time_t>(dt.toUTC().toSecsSinceEpoch()), 0,
        icaltimezone_get_utc_timezone());
}

qint64 durationSecs(icalcomponent *ev)
{
    const icaltimetype ds = icalcomponent_get_dtstart(ev);
    const icaltimetype de = icalcomponent_get_dtend(ev);
    const QDateTime a = toQt(ds);
    const QDateTime b = toQt(de);
    if (a.isValid() && b.isValid() && b > a)
        return a.secsTo(b);
    const struct icaldurationtype dur = icalcomponent_get_duration(ev);
    if (!icaldurationtype_is_null_duration(dur))
        return icaldurationtype_as_int(dur);
    return icaltime_is_date(ds) ? 86400 : 0;
}

QString propText(icalcomponent *ev, icalproperty_kind kind)
{
    icalproperty *p = icalcomponent_get_first_property(ev, kind);
    return p ? qs(icalproperty_get_value_as_string(p)) : QString();
}

bool cancelled(icalcomponent *ev)
{
    return icalcomponent_get_status(ev) == ICAL_STATUS_CANCELLED;
}

struct ExpandCtx {
    QList<EventOccurrence> *out = nullptr;
    QString calendarName, uid, summary, location, description;
    QDateTime from, to;
    bool allDay = false;
    const QSet<QDateTime> *overrides = nullptr;
};

// libical 3.x: callback получает icaltime_span (start/end — секунды UTC).
void collectOccurrence(icalcomponent *, icaltime_span *span, void *data)
{
    auto *ctx = static_cast<ExpandCtx *>(data);
    if (!span)
        return;
    QDateTime s, e;
    if (ctx->allDay) {
        // DATE-значения — полночь UTC; берём именно дату, чтобы пояс не сдвигал день
        const QDate ds = QDateTime::fromSecsSinceEpoch(span->start, Qt::UTC).date();
        const QDate de = QDateTime::fromSecsSinceEpoch(span->end, Qt::UTC).date();
        s = QDateTime(ds, QTime(0, 0), Qt::LocalTime);
        e = QDateTime(de, QTime(0, 0), Qt::LocalTime);
    } else {
        s = QDateTime::fromSecsSinceEpoch(span->start, Qt::UTC).toLocalTime();
        e = QDateTime::fromSecsSinceEpoch(span->end, Qt::UTC).toLocalTime();
    }
    if (!s.isValid())
        return;
    if (e <= ctx->from || s >= ctx->to)
        return;
    if (ctx->overrides && ctx->overrides->contains(s.toUTC()))
        return; // есть переопределённый экземпляр — он добавится отдельно
    EventOccurrence ev;
    ev.uid = ctx->uid;
    ev.summary = ctx->summary;
    ev.location = ctx->location;
    ev.description = ctx->description;
    ev.calendarName = ctx->calendarName;
    ev.start = s;
    ev.end = e;
    ev.allDay = ctx->allDay;
    ctx->out->append(ev);
}

} // namespace

QList<EventOccurrence> ICalParser::parse(const QByteArray &data, const QString &calendarName,
                                         const QDateTime &from, const QDateTime &to)
{
    QList<EventOccurrence> out;
    icalcomponent *root = icalcomponent_new_from_string(data.constData());
    if (!root)
        return out;

    QList<icalcomponent *> masters, overrides;
    QHash<QString, QSet<QDateTime>> overrideIds;

    for (icalcomponent *ev = icalcomponent_get_first_component(root, ICAL_VEVENT_COMPONENT);
         ev; ev = icalcomponent_get_next_component(root, ICAL_VEVENT_COMPONENT)) {
        if (cancelled(ev))
            continue;
        if (icalcomponent_get_first_property(ev, ICAL_RECURRENCEID_PROPERTY)) {
            overrides.append(ev);
            const QDateTime rid = toQt(icalcomponent_get_recurrenceid(ev));
            if (rid.isValid())
                overrideIds[qs(icalcomponent_get_uid(ev))].insert(rid.toUTC());
        } else {
            masters.append(ev);
        }
    }

    const icaltimetype rStart = fromQtUtc(from);
    const icaltimetype rEnd = fromQtUtc(to);

    for (icalcomponent *ev : masters) {
        const icaltimetype ds = icalcomponent_get_dtstart(ev);
        if (icaltime_is_null_time(ds))
            continue;
        ExpandCtx ctx;
        ctx.out = &out;
        ctx.calendarName = calendarName;
        ctx.from = from;
        ctx.to = to;
        ctx.uid = qs(icalcomponent_get_uid(ev));
        ctx.summary = qs(icalcomponent_get_summary(ev));
        ctx.location = propText(ev, ICAL_LOCATION_PROPERTY);
        ctx.description = propText(ev, ICAL_DESCRIPTION_PROPERTY);
        ctx.allDay = icaltime_is_date(ds);
        const QSet<QDateTime> ids = overrideIds.value(ctx.uid);
        ctx.overrides = &ids;
        icalcomponent_foreach_recurrence(ev, rStart, rEnd, collectOccurrence, &ctx);
    }

    // Переопределённые экземпляры (изменённое время/название одного из повторов)
    for (icalcomponent *ev : overrides) {
        const icaltimetype ds = icalcomponent_get_dtstart(ev);
        const QDateTime s = toQt(ds);
        if (!s.isValid())
            continue;
        const QDateTime e = s.addSecs(durationSecs(ev));
        if (e <= from || s >= to)
            continue;
        EventOccurrence o;
        o.uid = qs(icalcomponent_get_uid(ev));
        o.summary = qs(icalcomponent_get_summary(ev));
        o.location = propText(ev, ICAL_LOCATION_PROPERTY);
        o.description = propText(ev, ICAL_DESCRIPTION_PROPERTY);
        o.calendarName = calendarName;
        o.start = s;
        o.end = e;
        o.allDay = icaltime_is_date(ds);
        out.append(o);
    }

    icalcomponent_free(root);
    return out;
}
