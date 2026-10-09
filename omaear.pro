QT += quick quickcontrols2 sql network dbus multimedia
CONFIG += c++17
CONFIG -= app_bundle

TARGET = omaear
# Single source of the app version (--version, User-Agent strings, package).
VERSION = 0.1.0
DEFINES += OMAEAR_VERSION=\\\"$$VERSION\\\"
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

# `make install`: default is the user prefix (~/.local, no root). Packaging:
#   qmake6 PREFIX=/usr && make && make INSTALL_ROOT="$pkgdir" install
isEmpty(PREFIX): PREFIX = $$(HOME)/.local
target.path = $$PREFIX/bin

# Desktop entry: plain `omaear` for a system install (/usr/bin is on PATH),
# the absolute path for the user prefix (launchers may not see ~/.local/bin).
equals(PREFIX, /usr): DESKTOP_EXEC = omaear
else: DESKTOP_EXEC = $$PREFIX/bin/omaear
QMAKE_SUBSTITUTES += com.github.allanjorch.omaear.desktop.in
desktop.files = $$OUT_PWD/com.github.allanjorch.omaear.desktop
desktop.CONFIG += no_check_exist
desktop.path = $$PREFIX/share/applications

license.files = LICENSE
license.path = $$PREFIX/share/licenses/omaear

INSTALLS += target desktop
equals(PREFIX, /usr): INSTALLS += license
