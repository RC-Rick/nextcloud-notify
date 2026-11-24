#include "MacNotify.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

#include <QByteArray>
#include <QDebug>

#include <atomic>

@class NCNotifyDelegate;

// Разрешение на уведомления приходит асинхронно; до ответа считаем, что
// доставка возможна, чтобы не терять напоминания в первые секунды работы.
namespace {

enum DeliveryState { StateUnknown = 0, StateGranted, StateDenied, StateUnavailable };

std::atomic<int> gState{StateUnknown};
NCNotifyDelegate *gDelegate = nil;
bool gInitialized = false;
NSString *const kNcUrlKey = @"ncUrl";

bool bundleIsUsable()
{
    NSString *ident = [[NSBundle mainBundle] bundleIdentifier];
    return ident != nil && [ident length] > 0;
}

bool modernApiAvailable()
{
    if (@available(macOS 10.14, *))
        return NSClassFromString(@"UNUserNotificationCenter") != nil && bundleIsUsable();
    return false;
}

NSString *toNSString(const QString &s)
{
    const QByteArray utf8 = s.toUtf8();
    return [NSString stringWithUTF8String:utf8.constData()];
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

void deliverLegacy(NSString *title, NSString *body, NSString *url, bool withSound)
{
    NSUserNotification *note = [[NSUserNotification alloc] init];
    note.title = title;
    note.informativeText = body;
    note.soundName = withSound ? NSUserNotificationDefaultSoundName : nil;
    if (url)
        note.userInfo = @{ kNcUrlKey : url };
    [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:note];
}

#pragma clang diagnostic pop

} // namespace

@interface NCNotifyDelegate : NSObject <UNUserNotificationCenterDelegate,
                                       NSUserNotificationCenterDelegate>
@end

@implementation NCNotifyDelegate

// --- UserNotifications ---

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
    didReceiveNotificationResponse:(UNNotificationResponse *)response
             withCompletionHandler:(void (^)(void))completionHandler
{
    NSString *url = response.notification.request.content.userInfo[kNcUrlKey];
    if ([url isKindOfClass:[NSString class]] && [url length] > 0)
        [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:url]];
    completionHandler();
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler
{
    if (@available(macOS 11.0, *)) {
        completionHandler(UNNotificationPresentationOptionBanner
                          | UNNotificationPresentationOptionList
                          | UNNotificationPresentationOptionSound);
    } else {
        completionHandler(UNNotificationPresentationOptionAlert
                          | UNNotificationPresentationOptionSound);
    }
}

// --- NSUserNotification (запасной путь для старых систем) ---

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

- (void)userNotificationCenter:(NSUserNotificationCenter *)center
       didActivateNotification:(NSUserNotification *)notification
{
    NSString *url = notification.userInfo[kNcUrlKey];
    if ([url isKindOfClass:[NSString class]] && [url length] > 0)
        [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:url]];
    [center removeDeliveredNotification:notification];
}

- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center
       shouldPresentNotification:(NSUserNotification *)notification
{
    return YES;
}

#pragma clang diagnostic pop

@end

namespace MacNotify {

void initialize()
{
    if (gInitialized)
        return;
    gInitialized = true;

    if (!bundleIsUsable()) {
        qInfo("nc-notify: нет bundle identifier (запуск вне .app) — "
              "нативные уведомления отключены, работает своё окно");
        gState = StateUnavailable;
        return;
    }

    gDelegate = [[NCNotifyDelegate alloc] init];

    if (modernApiAvailable()) {
        UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
        center.delegate = gDelegate;
        [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert
                                                | UNAuthorizationOptionSound
                                                | UNAuthorizationOptionBadge)
                              completionHandler:^(BOOL granted, NSError *error) {
            gState = granted ? StateGranted : StateDenied;
            if (error) {
                qWarning("nc-notify: запрос разрешения на уведомления: %s "
                         "(работает запасное окно)",
                         error.localizedDescription.UTF8String);
            } else {
                qInfo("nc-notify: разрешение на уведомления: %s",
                      granted ? "выдано" : "запрещено");
            }
        }];
        return;
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [NSUserNotificationCenter defaultUserNotificationCenter].delegate = gDelegate;
#pragma clang diagnostic pop
    gState = StateGranted;
}

bool canDeliver()
{
    const int state = gState.load();
    return state == StateGranted || state == StateUnknown;
}

bool send(const QString &title, const QString &body, const QString &identifier,
          const QString &url, bool withSound)
{
    if (!bundleIsUsable())
        return false;

    NSString *nsTitle = toNSString(title);
    NSString *nsBody = toNSString(body);
    NSString *nsId = identifier.isEmpty() ? [[NSUUID UUID] UUIDString] : toNSString(identifier);
    NSString *nsUrl = url.isEmpty() ? nil : toNSString(url);

    const int state = gState.load();
    if (state == StateDenied || state == StateUnavailable)
        return false;

    if (modernApiAvailable()) {
        UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
        content.title = nsTitle;
        content.body = nsBody;
        if (withSound)
            content.sound = [UNNotificationSound defaultSound];
        content.userInfo = nsUrl ? @{ kNcUrlKey : nsUrl } : @{};

        UNNotificationRequest *request =
            [UNNotificationRequest requestWithIdentifier:nsId content:content trigger:nil];

        @try {
            [[UNUserNotificationCenter currentNotificationCenter]
                addNotificationRequest:request
                     withCompletionHandler:^(NSError *error) {
                if (error)
                    qWarning("nc-notify: уведомление не доставлено: %s",
                             error.localizedDescription.UTF8String);
            }];
            return true;
        } @catch (NSException *e) {
            qWarning("nc-notify: исключение при постановке уведомления: %s",
                     e.reason.UTF8String);
            return false;
        }
    }

    deliverLegacy(nsTitle, nsBody, nsUrl, withSound);
    return true;
}

} // namespace MacNotify
