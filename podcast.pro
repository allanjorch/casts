QT += quick quickcontrols2 sql network dbus multimedia
CONFIG += c++17
CONFIG -= app_bundle

TARGET = podcast
TEMPLATE = app

SOURCES += \
    src/main.cpp \
    src/library.cpp \
    src/feed.cpp \
    src/theme.cpp \
    src/models.cpp \
    src/covercache.cpp \
    src/backend.cpp \
    src/player.cpp \
    src/growingmediadevice.cpp \
    src/selftest.cpp \
    src/explore.cpp

HEADERS += \
    src/library.h \
    src/feed.h \
    src/theme.h \
    src/models.h \
    src/covercache.h \
    src/backend.h \
    src/player.h \
    src/growingmediadevice.h \
    src/explore.h \
    src/netaccess.h

RESOURCES += resources.qrc

# Installed by `make install` into the user prefix, no root required.
PREFIX = $$getenv(HOME)/.local
target.path = $$PREFIX/bin
desktop.files = com.github.allanjorch.podcast.desktop
desktop.path = $$PREFIX/share/applications
INSTALLS += target desktop
