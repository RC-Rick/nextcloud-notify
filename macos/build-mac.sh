#!/bin/bash
# Сборка NC Notify.app и DMG для macOS. Запуск: bash macos/build-mac.sh
# Требования: Xcode Command Line Tools, Homebrew с qt@5, libical, pkgconf.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
die() { echo "ОШИБКА: $*" >&2; exit 1; }

command -v brew >/dev/null 2>&1 || die "не найден Homebrew"
QT_PREFIX="$(brew --prefix qt@5 2>/dev/null)" || die "нет qt@5 в Homebrew: brew install qt@5"
QMAKE="$QT_PREFIX/bin/qmake"
MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"
[ -x "$QMAKE" ] || die "не найден qmake в $QT_PREFIX"
[ -x "$MACDEPLOYQT" ] || die "не найден macdeployqt в $QT_PREFIX"
for c in pkg-config python3 make iconutil hdiutil codesign sips; do
    command -v "$c" >/dev/null 2>&1 || die "не найдена команда: $c"
done

# libical зависит от keg-only ICU: её .pc не виден без явного пути.
ICU_PC="$(brew --prefix icu4c 2>/dev/null)/lib/pkgconfig"
export PKG_CONFIG_PATH="$ICU_PC:$(brew --prefix)/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
pkg-config --exists libical || die "libical не найден через pkg-config: brew install libical"

VERSION="$(sed -n 's/.*setApplicationVersion(QStringLiteral("\([^"]*\)")).*/\1/p' src/main.cpp | head -n1)"
[ -n "$VERSION" ] || die "не удалось определить версию из src/main.cpp"

BUILD="$ROOT/macos/build"
DIST="$ROOT/dist"
APP="NC Notify.app"
BUNDLE="$BUILD/qmake/nc-notify.app"

echo "== NC Notify $VERSION для macOS =="
echo "== 1/6 Иконки =="
python3 - "$BUILD/nc-notify.iconset" <<'PY'
import os, sys
sys.path.insert(0, "tools")
import gen_icons as g

iconset = sys.argv[1]
os.makedirs(iconset, exist_ok=True)
for size in (16, 32, 64, 128, 256, 512):
    g.write_png(os.path.join(iconset, "icon_%dx%d.png" % (size, size)), size)
for size in (16, 32, 128, 256, 512):
    g.write_png(os.path.join(iconset, "icon_%dx%d@2x.png" % (size, size)), size * 2)
PY
iconutil -c icns "$BUILD/nc-notify.iconset" -o "$ROOT/macos/nc-notify.icns"
echo "  macos/nc-notify.icns"

echo "== 2/6 qmake =="
mkdir -p "$BUILD/qmake"
cd "$BUILD/qmake"
"$QMAKE" "$ROOT/macos/nc-notify-mac.pro" -spec macx-clang \
    CONFIG+=release CONFIG+=sdk_no_version_check >/dev/null
cd "$ROOT"

echo "== 3/6 Сборка =="
# qmake не всегда перезаписывает Info.plist в уже собранном бандле — убираем,
# чтобы изменения метаданных (идентификатор, версия) точно попали в .app.
rm -f "$BUNDLE/Contents/Info.plist"
make -C "$BUILD/qmake" -j"$(sysctl -n hw.ncpu)" >/dev/null
[ -d "$BUNDLE" ] || die "бандл не собран: $BUNDLE"
BIN="$BUNDLE/Contents/MacOS/nc-notify"
[ -x "$BIN" ] || die "нет исполняемого файла в бандле"
echo "  $("$BIN" --version) [$($BIN --curl-info 2>&1 | tr '\n' ';')]"

echo "== 4/6 Qt frameworks =="
"$MACDEPLOYQT" "$BUNDLE" -always-overwrite >/dev/null 2>&1 || \
    die "macdeployqt завершился с ошибкой"

# macdeployqt переносит Qt и прямые зависимости бинарника, но не всегда
# забирает транзитивные библиотеки (например libbrotlicommon для libbrotlidec).
# Закрываем граф зависимостей: dylib из Homebrew копируем в Frameworks,
# абсолютные пути переписываем на @executable_path.
FRAMEWORKS="$BUNDLE/Contents/Frameworks"
find_brew_lib() {
    local base="$1"
    local p
    p="$(ls "$(brew --prefix)/lib/$base" 2>/dev/null | head -n1)"
    [ -n "$p" ] && { echo "$p"; return 0; }
    p="$(ls "$(brew --prefix)"/opt/*/lib/"$base" 2>/dev/null | head -n1)"
    [ -n "$p" ] && { echo "$p"; return 0; }
    return 1
}
missing=0
for pass in 1 2 3 4 5; do
    changed=0
    while IFS= read -r f; do
        while IFS= read -r dep; do
            [ -n "$dep" ] || continue
            case "$dep" in
                /usr/lib/*|/System/*) continue ;;
                @executable_path/*|@loader_path/*) continue ;;
                @rpath/*)
                    base="$(basename "$dep")"
                    [ -e "$FRAMEWORKS/$base" ] && continue
                    if src="$(find_brew_lib "$base")"; then
                        cp -f "$src" "$FRAMEWORKS/$base"
                        chmod 0755 "$FRAMEWORKS/$base"
                        changed=1
                    else
                        echo "  ПРЕДУПРЕЖДЕНИЕ: нет $dep (из $(basename "$f"))"
                        missing=1
                    fi
                    ;;
                *.framework/*) continue ;;
                *)
                    base="$(basename "$dep")"
                    [ -e "$dep" ] || { echo "  ПРЕДУПРЕЖДЕНИЕ: нет $dep"; missing=1; continue; }
                    if [ ! -e "$FRAMEWORKS/$base" ]; then
                        cp -f "$dep" "$FRAMEWORKS/$base"
                        chmod 0755 "$FRAMEWORKS/$base"
                        install_name_tool -id "@executable_path/../Frameworks/$base" \
                            "$FRAMEWORKS/$base"
                        changed=1
                    fi
                    install_name_tool -change "$dep" \
                        "@executable_path/../Frameworks/$base" "$f" 2>/dev/null || true
                    ;;
            esac
        done < <(otool -L "$f" 2>/dev/null | awk 'NR>1 {print $1}')
    done < <(find "$BUNDLE/Contents" -type f \( -perm -u+x -o -name '*.dylib' \) -print)
    [ "$changed" -eq 0 ] && break
done
[ "$missing" -eq 0 ] || echo "  ВНИМАНИЕ: часть зависимостей не найдена (см. выше)"
echo "  библиотек в Frameworks: $(ls "$FRAMEWORKS" 2>/dev/null | wc -l | tr -d ' ')"

echo "== 5/6 Подпись (ad-hoc) =="
codesign --force --deep --sign - --timestamp=none "$BUNDLE" 2>/dev/null
codesign --verify --deep --strict "$BUNDLE" || die "подпись не прошла проверку"
echo "  подписано ad-hoc (без Developer ID)"

echo "== 6/6 DMG =="
mkdir -p "$DIST"
STAGE="$BUILD/dmg"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R "$BUNDLE" "$STAGE/$APP"
ln -s /Applications "$STAGE/Applications"
DMG="$DIST/NC-Notify-${VERSION}.dmg"
rm -f "$DMG"
hdiutil create -quiet -volname "NC Notify" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
(cd "$DIST" && shasum -a 256 "$(basename "$DMG")" > "$(basename "$DMG").sha256")

echo
echo "ГОТОВО: $DMG"
echo "Установка: открыть DMG и перетащить «${APP}» в Applications."
echo "Первый запуск скачанного DMG: правый клик по приложению → «Открыть»."
