# Product

<!-- impeccable:product-schema 1 -->

## Platform

windows-desktop
<!-- Fuera de los cuatro valores estándar (web/ios/android/adaptive): es una app
     nativa Win32 + GDI+ de un solo OS. No aplica ninguna referencia nativa móvil. -->

## Stack

C++17 + Win32 + GDI+ en un único archivo (`native/GuitarAmpNative.cpp`), compilado
con `cl /O2` (ver `_build.ps1`). Audio por WASAPI event-driven en un hilo aparte.
No usa framework de UI ni webview: toda la interfaz son trazos GDI+ por código.

## Users

- Guitarrista en directo (ensayo/escenario): cambia de ampli y de cadena en segundos.
- Grabación en estudio: compara tonos y ajusta gain/EQ/cabina con calma.
- Práctica en casa: recorre presets y trastea con el sonido.
- Enseñar la app a otros: la propia UI es la demo, debe impresionar al abrirla.

## Product Purpose

Simulador de amplificadores de guitarra con tres familias de sonido (clean, crunch,
alta ganancia) y una cadena de efectos real: pedales antes y después del ampli,
cabina con IR, afinador y grabación a WAV. El éxito es construir un rig completo y
escucharlo cambiar en vivo, en una sola pantalla, sin scroll ni submenús.

## Positioning

El frontales reales de cada amplificador (arte raster 1200×540 con sus perillas y
filmstrips) son el material del producto y se reutilizan como hero y como miniatura
de la lista: el mismo idioma visual en la navegación y en el escenario. Un plugin
genérico dibuja knobs y etiquetas; aquí el ampli es una pieza de mobiliario que se
reconoce de un vistazo.

## Operating Context

- Sesiones cortas y repetidas: cambiar de ampli, probar la cadena, guardar un preset.
- Se usa con la guitarra puesta: una mano en el instrumento, otra en el ratón o el teclado.
- Entornos con luz baja (estudio, escenario): la interfaz se lee sobre fondo oscuro.
- El usuario compara A/B tonos, así que el estado actual debe ser legible de un vistazo.

## Capabilities and Constraints

- 13 amplificadores: 12 voces con 6 knobs propios, arte frontal real y presets de
  fábrica, mas el cabezal PERFIL (cadenas NAM: NAM1 y NAM2/A2).
- 18 tipos de pedal, repartidos en dos filas: **antes del ampli** y **después del ampli**.
- Cabina: 6 modelos procedurales + IR de usuario (WAV), 3 micrófonos, 3 distancias, MEZCLA.
- Presets de fábrica y de usuario (guardar, renombrar, borrar), favoritos por ampli.
- Afinador modal, grabación a WAV, ajustes de audio (dispositivo, WASAPI, rate, buffer).
- Una sola ventana de 1440×956 como **tamaño preferido**, redimensionable y que
  **siempre cabe en pantalla**: el diseño se escala al área cliente (transformación
  de GDI+, así dibujo e hit-test no se desalinean) y el mínimo son 1120×760.
  Motivo: en un portátil 1366×768 una ventana fija de 1440×956 se centraba en
  y negativo y el bridge y la línea de estado quedaban fuera, sin scroll.
- Atajos confirmados: ESPACIO (audio), ←/→ (preset), ↑/↓ (lista de la izquierda:
  amplis o presets), ESC (cerrar modal), arrastre de knobs y de pedales, rueda
  sobre knobs y lista.
- Audio: WASAPI event-driven, 44.1–192 kHz, buffer configurable, latencia declarada en ms.

## Brand Commitments

- Nombre visible de producto: **GUITAR ONE** (wordmark en la cabecera) sobre la app
  "GuitarAmp". No inventar claims de terceros ni marcas de amplificadores reales.
- **El arte de los 12 frontales es intocable** (fondos, filmstrips y posiciones de
  perilla): el rediseño solo corrige defectos, nunca lo reemplaza.
- El pedalboard de 18 FX y su disposición en dos filas antes/después se conserva.

## Evidence on Hand

- Arte de amplis: `native/amps_frontales/` (fondos, perillas, filmstrips, preview) +
  posiciones en `native/amp_frontales_data.h`. Regenerable con `generador_frontales.py`.
- Bancos de sonido y presets: `native/amp_presets_data.h` (12 amps × 6 presets).
- Capturas de referencia: `_app_shot.png` (app), `_diseno_shot.png` (mockup previo).
- No hay testimonios, clientes, precios ni licencias comerciales: no inventarlos.

## Product Principles

1. El ampli es el héroe: el arte real manda y la interfaz no compite con él.
2. Una sola pantalla: todo lo que cambia el sonido está a un gesto, sin scroll.
3. El estado se lee sin tocar: valores, niveles y selección siempre visibles.
4. El mismo idioma visual en lista, escenario y cadena: nada decorativo sin función.
5. Silencio visual: el único color vivo marca lo interactivo o lo que suena.

## Accessibility & Inclusion

- La app se maneja **con teclado además del ratón** en las dos cosas que más se
  usan: ESPACIO enciende/apaga, ←/→ cambian de preset y ↑/↓ recorren la lista de
  amplis o presets (el resaltado de fila hace de foco visible). Queda pendiente el
  anillo de foco para mandos y botones (no hay Tab en la app todavía).
- Objetivos de clic cómodos: los mandos se arrastran y aceptan rueda; los botones
  de borrar de los presets y de los bloques NAM piden confirmación o dos clics.
- Texto secundario legible sobre el fondo oscuro: contraste ≥4.5:1 para texto pequeño.
- Toda la UI es dibujo propio: no hay árbol de accesibilidad de sistema; el rediseño
  no debe empeorar el manejo por teclado ni el tamaño de las dianas.
