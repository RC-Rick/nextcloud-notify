#!/bin/bash
# Сборка .deb пакета nc-notify. Запуск: bash packaging/build-deb.sh [версия]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
die() { echo "ОШИБКА: $*" >&2; exit 1; }

for c in qmake make dpkg-deb python3 install gzip du strip ldd; do
    command -v "$c" >/dev/null 2>&1 || die "не найдена команда: $c"
done
export QT_SELECT=qt5

VERSION="${1:-$(sed -n 's/.*setApplicationVersion(QStringLiteral("\([^"]*\)")).*/\1/p' src/main.cpp | head -n1)}"
[ -n "$VERSION" ] || die "не удалось определить версию"
MAINTAINER="$(head -n1 packaging/maintainer)"
case "$MAINTAINER" in
    *localhost*|"") die "укажите корректный контакт в packaging/maintainer" ;;
esac
ARCH="$(dpkg --print-architecture)"
NAME="nc-notify_${VERSION}_${ARCH}"
BUILD="$ROOT/build-release"
STAGE="$BUILD/pkg"
DIST="$ROOT/dist"

echo "== nc-notify $VERSION ($ARCH) =="
echo "== 1/6 Сборка (release) =="
mkdir -p "$BUILD"
cd "$BUILD"
qmake "$ROOT/nc-notify.pro" CONFIG+=release
make -j"$(nproc)" >/dev/null
[ -x "$BUILD/nc-notify" ] || die "бинарник не собран"
cd "$ROOT"

echo "== 2/6 Иконки =="
python3 tools/gen_icons.py >/dev/null
echo "  $(ls icons/hicolor | tr '\n' ' ')"

echo "== 3/6 Дерево пакета =="
rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" "$STAGE/usr/bin" "$STAGE/usr/share/applications" \
         "$STAGE/usr/share/man/man1" "$STAGE/usr/share/doc/nc-notify"
install -m 0755 -s "$BUILD/nc-notify" "$STAGE/usr/bin/nc-notify"
install -m 0644 packaging/nc-notify.desktop "$STAGE/usr/share/applications/nc-notify.desktop"
gzip -9n -c packaging/nc-notify.1 > "$STAGE/usr/share/man/man1/nc-notify.1.gz"
for d in icons/hicolor/*; do
    [ -d "$d" ] || continue
    size="$(basename "$d")"
    mkdir -p "$STAGE/usr/share/icons/hicolor/$size/apps"
    for f in "$d/apps/nc-notify.png" "$d/apps/nc-notify.svg"; do
        [ -f "$f" ] && install -m 0644 "$f" "$STAGE/usr/share/icons/hicolor/$size/apps/"
    done
done
install -m 0644 packaging/copyright "$STAGE/usr/share/doc/nc-notify/copyright"
sed "s/@MAINTAINER@/$MAINTAINER/g" packaging/changelog | gzip -9n -c \
    > "$STAGE/usr/share/doc/nc-notify/changelog.gz"

echo "== 4/6 Зависимости =="
DEPS="$(python3 packaging/deps.py "$STAGE/usr/bin/nc-notify")"
if [ -z "$DEPS" ]; then
    DEPS="libc6, libstdc++6, libqt5core5a, libqt5gui5, libqt5widgets5, libqt5network5, libqt5dbus5, libqt5concurrent5, libical3, libqt5keychain1, libcanberra0, libcurl4"
    echo "  ВНИМАНИЕ: использован резервный список"
fi
echo "  Depends: $DEPS"
SIZE_KB="$(du -sk "$STAGE/usr" | cut -f1)"

cat > "$STAGE/DEBIAN/control" <<CTRL
Package: nc-notify
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Installed-Size: $SIZE_KB
Depends: $DEPS
Recommends: gnome-keyring, libqt5svg5
Maintainer: $MAINTAINER
Description: Nextcloud calendar and Deck reminders for Astra Linux Fly
 Tray application: syncs Nextcloud calendars over CalDAV and Deck cards
 over the Deck API, lists upcoming events and cards, sends reminders via
 fly-notifications and its own popup window. Passwords are stored in the
 system keyring; autostart and single-instance are supported.
CTRL

cat > "$STAGE/DEBIAN/postinst" <<'POST'
#!/bin/sh
set -e
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || true
fi
exit 0
POST
cp "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"
chmod 0755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"

echo "== 5/6 Сборка пакета =="
mkdir -p "$DIST"
dpkg-deb --build --root-owner-group "$STAGE" "$DIST/$NAME.deb"

echo "== 6/6 Контрольная сумма и проверка =="
(cd "$DIST" && sha256sum "$NAME.deb" > "$NAME.deb.sha256")
if command -v lintian >/dev/null 2>&1; then
    echo "-- lintian --"
    lintian "$DIST/$NAME.deb" || true
fi
echo
echo "ГОТОВО: $DIST/$NAME.deb"
dpkg-deb -f "$DIST/$NAME.deb" Package Version Depends
