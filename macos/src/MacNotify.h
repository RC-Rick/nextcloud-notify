#pragma once

#include <QString>

// Нативные уведомления macOS: UserNotifications (10.14+) с откатом на
// устаревший NSUserNotification. Клик по баннеру открывает URL события.
namespace MacNotify {

// Ставит делегата и запрашивает разрешение. Вызывать один раз после старта.
void initialize();

// false — доставлять нельзя (нет разрешения или приложение запущено
// не из .app-бандла): вызывающий код должен показать своё окно.
bool canDeliver();

bool send(const QString &title, const QString &body, const QString &identifier,
          const QString &url, bool withSound);

} // namespace MacNotify
