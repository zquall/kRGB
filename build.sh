#!/bin/bash
#---------------------------------------------------------------------------#
# build.sh  —  Compila el CLI (krgb) y el app de barra (kRGB.app)           #
#              Cero dependencias externas: solo frameworks del sistema.      #
#---------------------------------------------------------------------------#
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

echo "==> CLI: krgb"
clang -O2 -Wall krgb.c engine.c -o krgb \
      -framework IOKit -framework CoreFoundation

echo "==> App: kRGB.app"
APP="kRGB.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
clang -O2 -Wall -fobjc-arc -I"$ROOT" app/main.m app/AppDelegate.m engine.c \
      -o "$APP/Contents/MacOS/kRGB" \
      -framework Cocoa -framework IOKit -framework CoreFoundation \
      -framework ServiceManagement
cp app/Info.plist "$APP/Contents/Info.plist"

# Firma ad-hoc (evita avisos de Gatekeeper al ser local).
codesign -s - --force "$APP" >/dev/null 2>&1 || true

echo ""
echo "Listo:"
echo "  App: $ROOT/$APP   (abrir con: open '$ROOT/$APP')"
echo "  CLI: $ROOT/krgb   (probar con: ./krgb wave)"
