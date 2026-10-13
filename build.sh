#!/bin/bash
#---------------------------------------------------------------------------#
# build.sh  —  Builds the CLI (krgb) and the menu bar app (kRGB.app)        #
#              Zero external dependencies: system frameworks only.          #
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

# Ad-hoc signature (avoids Gatekeeper warnings for a local build).
codesign -s - --force "$APP" >/dev/null 2>&1 || true

echo ""
echo "Done:"
echo "  App: $ROOT/$APP   (open with: open '$ROOT/$APP')"
echo "  CLI: $ROOT/krgb   (try with: ./krgb wave)"
