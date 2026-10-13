# kRGB

Native, lightweight control of **Corsair Gaming K65 LUX RGB** keyboard lighting from the macOS menu bar.

No iCUE, no OpenRGB, and **zero external dependencies** — it talks to the keyboard directly using nothing but system frameworks.

---

## Why it exists

iCUE freezes on Intel Macs, and OpenRGB — while it does change colors — never managed to load its effect plugins. kRGB skips both: it implements the keyboard's protocol over IOKit HID in plain C.

The result is an ~80 KB app that uses ~21 MB of RAM and sits quietly in the menu bar.

---

## Effects

| | Effect | What it does |
|---|---|---|
| 🌈 | **Rainbow wave** | A rainbow wave sweeping across the keyboard. Directional. |
| 🌧️ | **Rain** (*Lluvia*) | Colored droplets falling down the columns, each with a fading tail. |
| 🟢 | **Matrix** | Green code cascade: near-white head, long tail that fades out. Directional. |
| 🌊 | **Waterfall** (*Catarata*) | A continuous rainbow curtain pouring across the board, with flowing water texture. Directional. |
| 🍄 | **Psychedelic** (*Psicodélico*) | A plasma field where colors blend and morph, never repeating. |
| 🐛 | **Bug** (*Bicho*) | A single lit key wandering around, bouncing off the edges, with a faint trail. |
| 🫧 | **Breathing** (*Respiración*) | The whole keyboard pulsing gently. |
| 🌀 | **Color cycle** (*Ciclo de color*) | The entire keyboard rotating through hues. |
| 🎨 | **Solid color** (*Color fijo*) | Blue, cyan, green, red, purple or white. |
| ⌨️ | **Off** (*Apagado*) | Hands control back to the keyboard's own built-in effects. |

**Speed:** 5 levels (very slow → very fast).

**Direction:** 4 orientations (left→right, right→left, top→bottom, bottom→top). Applies to Rainbow wave, Matrix and Waterfall.

> The app's menu is in Spanish; English names above are given for reference.

---

## Requirements

- **macOS 10.13 or later** (tested on 14.7.5 Sonoma, Intel x86_64).
  macOS 13+ is only needed for the *launch at login* option.
- **Xcode Command Line Tools** to build:
  ```bash
  xcode-select --install
  ```
- A **Corsair Gaming K65 LUX RGB** (VID `0x1B1C`, PID `0x1B37`).

No special macOS permissions are required — no Input Monitoring, no Accessibility.

---

## Installation

```bash
git clone https://github.com/zquall/kRGB.git
cd kRGB
./build.sh
open kRGB.app
```

An icon appears in the menu bar (an emoji reflecting the active effect). To install it permanently:

```bash
cp -R kRGB.app /Applications/
```

Then enable **Arrancar con la Mac** ("launch at login") from the menu so it starts automatically.

---

## Usage

Click the menu bar icon:

```
kRGB — Psicodélico
────────────────────────────────
  Rainbow wave
  Lluvia               (rain)
  Matrix
  Catarata             (waterfall)
✓ Psicodélico          (psychedelic)
  Bicho                (bug)
  Respiración          (breathing)
  Ciclo de color       (color cycle)
  Color fijo         ▸ (solid color)
  Apagado              (off)
────────────────────────────────
  Velocidad          ▸ (speed)
  Dirección          ▸ (direction)
────────────────────────────────
  Arrancar con la Mac  (launch at login)
  Reintentar conexión  (reconnect)
  Salir                (quit)
```

On quit, kRGB hands control back to the keyboard so it resumes its own hardware effects.

### Automatic reconnection

If you unplug the keyboard or the Mac goes to sleep, the icon switches to ⚠️ and kRGB retries the connection every ~2 seconds. When the keyboard comes back, it picks up the effect you had — no action needed.

---

## CLI

Besides the app, `build.sh` produces a command-line binary that's handy for diagnostics:

```bash
./krgb list              # List the keyboard's HID interfaces
./krgb color 255 0 0     # Paint the whole keyboard (values 0..255)
./krgb wave              # Animated rainbow (Ctrl-C to stop)
./krgb hardware          # Hand control back to the keyboard
```

---

## How it works

The keyboard exposes several HID interfaces. The **control** one is the interface whose `PrimaryUsagePage == 0xFFC2` — not the typing interface (`0x0001`). kRGB opens it through IOKit and sends it 64-byte *Output* reports:

1. **Software mode** (`07 05 02`) so the keyboard accepts external colors instead of running its internal effects.
2. **Color per channel**: every frame is split into three channels (R, G and B). Each channel is sent as *stream* packets (`0x7F`) of 60 + 60 + 24 bytes, followed by a *submit* (`0x28`).
3. **Hardware mode** (`07 04 01`) on exit, handing control back to the keyboard.

Each key's physical position is derived by inverting OpenRGB's `matrix_map[6][23]`, which is what makes the directional waves, the waterfall and the bug possible.

### Layout

```
engine.c / engine.h   Plain-C engine: IOKit, protocol and effects
krgb.c                Diagnostic CLI
app/                  Menu bar app (Objective-C / AppKit)
build.sh              Builds both with clang
```

It links only against `IOKit`, `CoreFoundation`, `Cocoa` and `ServiceManagement`.

---

## Other Corsair keyboards

The `keys[]` table matches OpenRGB's *"NORMAL"* layout, shared by several models in the LUX line. To try another keyboard, change `K65_LUX_PID` in `engine.c` to your model's PID and rebuild. It may work as-is, or it may need `keys[]` and `matrix_map` adjusted.

---

## Known limitations

- **Settings are not persisted:** on restart the app goes back to Rainbow wave at normal speed.
- **Vertical cascades don't read as straight columns.** Keyboard keys are physically staggered, so a descending line looks ragged. That's a hardware constraint, not a bug.
- Only tested on a K65 LUX RGB (firmware 3.08) running macOS Sonoma on Intel.

---

## License and credits

Released under **GPL-2.0**.

The keyboard protocol is ported from the `CorsairPeripheralController` driver in [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB), licensed under GPL-2.0 — the `keys[]` table and the geometry matrix come from there too. All credit for reverse-engineering the protocol is theirs.
