#!/usr/bin/env bash
# Збирає пакети Фініка для встановлення без сирців: .deb, .tar.gz і за наявності
# linuxdeploy — AppImage. GitHub Actions навмисно не викликає цей скрипт (CI на холді);
# артефакти кладуть у GitHub Release вручну.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${FINIK_BUILD_DIR:-$ROOT/build-package}"
DIST="$ROOT/dist"

cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD" -j"$(nproc)" --target Finik

rm -rf "$DIST"
mkdir -p "$DIST/AppDir"
cmake --install "$BUILD" --prefix "$DIST/AppDir/usr"

# CPack пише .deb/.tar.gz поруч із білдом.
cmake --build "$BUILD" --target package
find "$BUILD" -maxdepth 1 \( -name 'finik*.deb' -o -name 'finik*.tar.gz' \) \
  -exec cp -a {} "$DIST/" \;

if command -v linuxdeploy >/dev/null 2>&1; then
  linuxdeploy \
    --appdir "$DIST/AppDir" \
    --desktop-file "$ROOT/packaging/finik.desktop" \
    --icon-file "$ROOT/packaging/icons/hicolor/256x256/apps/finik.png" \
    --output appimage
  find "$ROOT" "$DIST" "$BUILD" -maxdepth 1 -name 'Finik*.AppImage' \
    -exec cp -a {} "$DIST/" \;
  echo "AppImage: $DIST"
else
  echo "linuxdeploy не знайдено — AppImage пропущено (є .deb/.tar.gz у $DIST)"
fi

echo "Готово. Каталог артефактів: $DIST"
ls -la "$DIST"
