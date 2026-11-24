QT += widgets network concurrent
TEMPLATE = app
TARGET = nc-notify
CONFIG += c++17 warn_on app_bundle link_pkgconfig
CONFIG -= app_bundle_empty_plist

VERSION = 1.0.1
QMAKE_MACOSX_DEPLOYMENT_TARGET = 11.0
QMAKE_INFO_PLIST = $$PWD/Info.plist
ICON = $$PWD/nc-notify.icns
QMAKE_OBJECTIVE_CFLAGS += -fobjc-arc

PKGCONFIG += libical libcurl
LIBS += -framework Security -framework CoreFoundation \
        -framework AppKit -framework UserNotifications

INCLUDEPATH += $$PWD/include $$PWD/src $$PWD/../src

DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x050F00

# Общие файлы проекта (без изменений)
SOURCES += \
    ../src/main.cpp \
    ../src/CalDavClient.cpp \
    ../src/CurlTransport.cpp \
    ../src/DeckClient.cpp \
    ../src/DeckWindow.cpp

# macOS-версии платформенных файлов
SOURCES += \
    src/ICalParser.cpp \
    src/MainWindow.cpp \
    src/SettingsDialog.cpp \
    src/NotificationManager.cpp \
    src/TrayController.cpp \
    src/Autostart.cpp \
    src/KeychainShim.cpp

OBJECTIVE_SOURCES += \
    src/MacNotify.mm

HEADERS += \
    ../src/MainWindow.h \
    ../src/SettingsDialog.h \
    ../src/CalDavClient.h \
    ../src/ICalParser.h \
    ../src/CurlTransport.h \
    ../src/DeckClient.h \
    ../src/DeckWindow.h \
    ../src/Event.h \
    ../src/NotificationManager.h \
    ../src/Autostart.h \
    ../src/TrayController.h \
    include/qt5keychain/keychain.h \
    src/MacNotify.h

RESOURCES += ../resources.qrc
