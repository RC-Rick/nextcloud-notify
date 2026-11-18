QT += widgets dbus network concurrent
CONFIG += c++17 warn_on link_pkgconfig
PKGCONFIG += libical libcurl
LIBS += -lqt5keychain -lcanberra
TARGET = nc-notify
TEMPLATE = app

SOURCES += \
    src/main.cpp \
    src/MainWindow.cpp \
    src/TrayController.cpp \
    src/CalDavClient.cpp \
    src/ICalParser.cpp \
    src/SettingsDialog.cpp \
    src/NotificationManager.cpp \
    src/Autostart.cpp \
    src/DeckClient.cpp \
    src/DeckWindow.cpp \
    src/CurlTransport.cpp

HEADERS += \
    src/MainWindow.h \
    src/TrayController.h \
    src/CalDavClient.h \
    src/ICalParser.h \
    src/SettingsDialog.h \
    src/Event.h \
    src/NotificationManager.h \
    src/Autostart.h \
    src/DeckClient.h \
    src/DeckWindow.h \
    src/CurlTransport.h

RESOURCES += resources.qrc
