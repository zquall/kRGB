# kRGB

Control nativo y ligero de la iluminación RGB del teclado **Corsair Gaming K65 LUX RGB** desde la barra de menú de macOS.

Sin iCUE, sin OpenRGB y **sin una sola dependencia externa**: habla directo con el teclado usando únicamente frameworks del sistema.

---

## ¿Por qué existe

iCUE se cuelga en Macs con procesador Intel, y OpenRGB —aunque sí cambia el color— no logra cargar los plugins de efectos. kRGB se salta ambos: implementa el protocolo del teclado sobre IOKit HID en C puro.

El resultado es un app de ~80 KB que consume ~21 MB de RAM y vive discretamente en la barra de menú.

---

## Efectos

| | Efecto | Qué hace |
|---|---|---|
| 🌈 | **Rainbow wave** | Ola de arcoíris recorriendo el teclado. Direccional. |
| 🌧️ | **Lluvia** | Gotas de colores cayendo por las columnas, con cola que se desvanece. |
| 🟢 | **Matrix** | Cascada de código verde: cabeza casi blanca y cola larga que se apaga. Direccional. |
| 🌊 | **Catarata** | Cortina continua de arcoíris que se derrama, con textura de agua. Direccional. |
| 🍄 | **Psicodélico** | Campo de plasma: los colores se funden y mutan sin repetirse nunca. |
| 🐛 | **Bicho** | Una sola tecla de luz que deambula rebotando en los bordes, con estela tenue. |
| 🫧 | **Respiración** | Todo el teclado latiendo suavemente. |
| 🌀 | **Ciclo de color** | El teclado completo girando de tono. |
| 🎨 | **Color fijo** | Azul, cian, verde, rojo, morado o blanco. |
| ⌨️ | **Apagado** | Devuelve el control a los efectos propios del teclado. |

**Velocidad:** 5 niveles (Muy lento → Muy rápido).

**Dirección:** 4 sentidos (izquierda→derecha, derecha→izquierda, arriba→abajo, abajo→arriba). Aplica a Rainbow wave, Matrix y Catarata.

---

## Requisitos

- **macOS 10.13 o superior** (probado en 14.7.5 Sonoma, Intel x86_64).
  macOS 13+ solo hace falta para la opción *Arrancar con la Mac*.
- **Xcode Command Line Tools** para compilar:
  ```bash
  xcode-select --install
  ```
- Un **Corsair Gaming K65 LUX RGB** (VID `0x1B1C`, PID `0x1B37`).

No requiere permisos especiales de macOS, ni Input Monitoring ni Accesibilidad.

---

## Instalación

```bash
git clone git@github.com:zquall/kRGB.git
cd kRGB
./build.sh
open kRGB.app
```

El ícono aparece en la barra de menú (un emoji según el efecto activo). Para dejarlo instalado de forma permanente:

```bash
cp -R kRGB.app /Applications/
```

Y activá **Arrancar con la Mac** desde el menú para que se levante solo al iniciar sesión.

---

## Uso

Hacé clic en el ícono de la barra de menú:

```
kRGB — Psicodélico
────────────────────────
  Rainbow wave
  Lluvia
  Matrix
  Catarata
✓ Psicodélico
  Bicho
  Respiración
  Ciclo de color
  Color fijo          ▸
  Apagado
────────────────────────
  Velocidad           ▸
  Dirección           ▸
────────────────────────
  Arrancar con la Mac
  Reintentar conexión
  Salir
```

Al salir, kRGB devuelve el control al teclado para que retome sus efectos de hardware.

### Reconexión automática

Si desenchufás el teclado o la Mac se duerme, el ícono cambia a ⚠️ y kRGB reintenta la conexión cada ~2 segundos. Cuando el teclado vuelve, retoma el efecto que tenías sin que tengas que hacer nada.

---

## CLI

Además del app, `build.sh` genera un binario de línea de comandos útil para diagnóstico:

```bash
./krgb list              # Lista las interfaces HID del teclado
./krgb color 255 0 0     # Pinta todo el teclado (valores 0..255)
./krgb wave              # Rainbow animado (Ctrl-C para parar)
./krgb hardware          # Devuelve el control al teclado
```

---

## Cómo funciona

El teclado expone varias interfaces HID. La de **control** es la que tiene `PrimaryUsagePage == 0xFFC2` — no la de tecleo (`0x0001`). kRGB la abre con IOKit y le envía reportes *Output* de 64 bytes:

1. **Modo software** (`07 05 02`) para que el teclado acepte colores externos en vez de usar sus efectos internos.
2. **Color por canal**: cada frame se parte en tres canales (R, G y B). Cada canal se manda en paquetes *stream* (`0x7F`) de 60 + 60 + 24 bytes, seguidos de un *submit* (`0x28`).
3. **Modo hardware** (`07 04 01`) al salir, para devolverle el mando al teclado.

La geometría física de cada tecla se obtiene invirtiendo la matriz `matrix_map[6][23]` de OpenRGB, lo que permite las olas direccionales, la catarata y el bicho.

### Estructura

```
engine.c / engine.h   Motor en C puro: IOKit, protocolo y efectos
krgb.c                CLI de diagnóstico
app/                  App de barra de menú (Objective-C / AppKit)
build.sh              Compila ambos con clang
```

Se enlaza solo contra `IOKit`, `CoreFoundation`, `Cocoa` y `ServiceManagement`.

---

## Otros teclados Corsair

El mapa de teclas `keys[]` corresponde al layout *"NORMAL"* de OpenRGB, compartido por varios modelos de la línea LUX. Para probar con otro teclado, cambiá `K65_LUX_PID` en `engine.c` por el PID de tu modelo y recompilá. Puede funcionar directamente o requerir ajustar `keys[]` y `matrix_map`.

---

## Limitaciones conocidas

- **No guarda la configuración:** al reiniciar el app vuelve a Rainbow wave en velocidad Normal.
- **Las cascadas verticales no se ven como columnas rectas.** Las teclas de un teclado están físicamente escalonadas (*stagger*), así que una línea que baja se percibe irregular. Es una limitación del hardware, no del código.
- Probado únicamente en un K65 LUX RGB (firmware 3.08) sobre macOS Sonoma en Intel.

---

## Licencia y créditos

Publicado bajo **GPL-2.0**.

El protocolo de comunicación con el teclado está portado del controlador `CorsairPeripheralController` de [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB), licenciado bajo GPL-2.0 — de ahí también provienen la tabla `keys[]` y la matriz de geometría. Todo el crédito del trabajo de ingeniería inversa del protocolo es suyo.
