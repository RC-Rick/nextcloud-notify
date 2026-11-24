# NC Notify для macOS

Порт `nc-notify` (Astra Linux / Qt5) на macOS: напоминания о событиях календаря
Nextcloud (CalDAV) и о сроках карточек Deck. Живёт в строке меню, показывает
список событий и карточек, присылает баннеры Центра уведомлений.

Ветка `macos` — отдельное ответвление от `develop`. Файлы Linux-сборки
(`nc-notify.pro`, `packaging/`, `src/`) не изменяются: macOS-версия собирается
своим `macos/nc-notify-mac.pro`, а платформенные файлы лежат в `macos/src`.

## Что заменено по сравнению с Linux-версией

| Узел | Linux | macOS |
|---|---|---|
| Уведомления | D-Bus `org.freedesktop.Notifications` | `UserNotifications` (10.14+), откат на `NSUserNotification`; клик по баннеру открывает событие |
| Своё окно напоминания | Qt-виджет | Qt-виджет в стиле macOS (светлая/тёмная тема) |
| Звук | libcanberra | `/usr/bin/afplay` + системный звук в баннере |
| Пароль | qtkeychain + gnome-keyring | `Security.framework` (Keychain), API QtKeychain сохранён шимом |
| Автозапуск | `~/.config/autostart/*.desktop` | LaunchAgent `~/Library/LaunchAgents/com.nc-notify.agent.plist` |
| Трей | QSystemTrayIcon | значок в строке меню (левый клик раскрывает меню) |
| Пакет | `.deb` | `NC Notify.app` в DMG, ad-hoc подпись |

Календарь, разбор iCalendar (libical), Deck API, pinning самоподписанного
сертификата, пороги напоминаний и защита от повторов — общий код из `src/`.

## Установка

1. Скачайте `NC-Notify-1.0.1.dmg` и файл `.sha256` со страницы релизов.
2. Проверьте контрольную сумму: `shasum -a 256 -c NC-Notify-1.0.1.dmg.sha256`.
3. Откройте DMG, перетащите **NC Notify** в **Applications**.
4. Первый запуск: правый клик по приложению → «Открыть» (пакет подписан ad-hoc,
   без Apple Developer ID, поэтому Gatekeeper спрашивает подтверждение).
5. Разрешите уведомления, когда macOS спросит. Без разрешения приложение
   автоматически показывает своё окно-напоминание.

## Первоначальная настройка

1. Nextcloud → аватар → Настройки → Безопасность → Устройства и сессии →
   создать пароль приложения `nc-notify`. Основной пароль не нужен.
2. Настройки приложения → вкладка «Аккаунт»: адрес сервера, логин, пароль
   приложения → «Найти календари» → отметить нужные. При самоподписанном
   сертификате подтвердите отпечаток SHA-256.
3. Вкладка «Deck»: включить карточки, «Найти доски», отметить доски.
4. Вкладка «Уведомления»: режим (баннер + окно / только баннер / только окно /
   тихо), звук, пороги «за сколько минут».
5. Вкладка «Общие»: автозапуск (LaunchAgent) и запуск свёрнутым в строку меню.

Закрытие окна прячет приложение в строку меню. Полный выход — «Выйти» в меню
значка или ⌘Q.

## Сборка из исходников

Нужны Xcode Command Line Tools и Homebrew.

    brew install qt@5 libical pkgconf
    git clone <адрес> nc-notify && cd nc-notify
    git checkout macos
    bash macos/build-mac.sh

Скрипт собирает `macos/build/qmake/nc-notify.app`, вкладывает Qt-фреймворки
(`macdeployqt`), подписывает ad-hoc и создаёт `dist/NC-Notify-1.0.1.dmg`
с контрольной суммой. Только сборка без DMG:

    mkdir -p macos/build/qmake && cd macos/build/qmake
    "$(brew --prefix qt@5)/bin/qmake" ../../macos/nc-notify-mac.pro -spec macx-clang
    make -j$(sysctl -n hw.ncpu)
    open nc-notify.app

## Ключи запуска

    nc-notify --version      версия программы
    nc-notify --minimized    запуск только в строку меню
    nc-notify --verbose      подробный вывод в терминал (stderr)
    nc-notify --curl-info    диагностика GSS-API/SPNEGO для Kerberos

Для бандла: `open -a "NC Notify" --args --verbose`.

## Хранение данных

- Настройки: `~/Library/Preferences/nc-notify.nc-notify.plist` (паролей нет).
- Пароль приложения: связка ключей macOS, служба `nc-notify`,
  учётная запись `caldavPassword`.
- Автозапуск: `~/Library/LaunchAgents/com.nc-notify.agent.plist`.
- Отпечаток сертификата сервера: в настройках (`tls/pin/<host>`).

## Известные ограничения

- Пакет не нотаризован Apple: при распространении через интернет Gatekeeper
  потребует «Открыть» через контекстное меню. Для распространения без
  предупреждений нужен Developer ID и нотаризация.
- Подпись ad-hoc меняется при каждой пересборке. Если macOS уже запомнила
  разрешение на уведомления для прошлой сборки, после обновления баннеры могут
  перестать приходить — тогда включите их заново в «Системные настройки →
  Уведомления → NC Notify». Напоминания при этом не теряются: при запрете
  уведомлений приложение автоматически показывает своё окно.
- По той же причине после обновления macOS может один раз спросить доступ
  к сохранённому паролю в Keychain — достаточно нажать «Разрешить».
- Первый запуск после сборки: macOS спрашивает разрешение на уведомления.
  Пока оно не выдано, работает запасное окно-напоминание.
- Сквозная авторизация Kerberos не протестирована в доменной среде; для Deck
  в этом режиме по-прежнему нужен пароль приложения.
- Цельнодневные события отображаются в списке, но не уведомляются.
- Уведомления работают только при запуске из `.app`-бандла (при запуске
  голого бинарника из `macos/build/qmake` остаётся своё окно).
- libcurl берётся системный (SecureTransport): проверка CA-файла в режиме
  Kerberos опирается на системное хранилище сертификатов.

## Структура

    macos/
      nc-notify-mac.pro      проект qmake для macOS
      Info.plist             метаданные бандла
      build-mac.sh           сборка .app + DMG
      include/qt5keychain/   шим QtKeychain поверх Keychain
      src/                   macOS-версии платформенных файлов
        MacNotify.{h,mm}     UserNotifications/NSUserNotification + клик по баннеру
        NotificationManager.cpp
        MainWindow.cpp       панель без иконок темы, текст «О программе»
        SettingsDialog.cpp   тексты про Keychain и LaunchAgent
        TrayController.cpp   меню в строке меню
        Autostart.cpp        LaunchAgent
        KeychainShim.cpp
