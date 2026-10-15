# GUITAR ONE — simulador de amplificadores (app de escritorio para Windows)

Aplicación **nativa de Windows (Win32 + GDI+, sin navegador)** con 13 amplificadores,
pedalboard, cabina con IR, afinador y grabación a WAV. **No es un plugin VST3**,
no necesita JUCE ni ningún framework.

> Antes este README describía un plugin JUCE de 3 amplificadores que ya no es el
> producto: ese árbol (`src/`, `CMakeLists.txt`, `build.ps1`, `standalone.html`,
> `preview.html`) está obsoleto. El producto vivo está en `native/`.

## Qué hay

- **13 amplificadores**: 12 voces modeladas (clean, crunch y alta ganancia) con su
  frontal real y su banco de 6 presets de fábrica (78 en total) más el cabezal
  **PERFIL**, que monta cadenas de capturas `.nam` del Neural Amp Modeler.
- **Pedalboard de 18 tipos** en dos filas, *antes* y *después* del ampli, con
  arrastre para reordenar y para cruzar el cabezal.
- **Cabina**: 6 modelos procedurales + IR de usuario desde WAV (PCM 8/16/24/32 y
  float 32/64, mono o estéreo), 3 micros × 3 distancias, MEZCLA, convolución
  overlap-save.
- **Cadena de capturas**: añadir `.nam` (locales o bajados de TONE3000), encender
  y apagar cada bloque, reordenarlo con `<` `>` y quitarlo con `x`.
- **Explorador TONE3000** con los mismos 7 filtros que su web, tendencias públicas
  sin clave, descarga a la cadena y lista pública de favoritos/favoritos.
- Afinador cromático modal, grabación a WAV, ajustes de audio (WASAPI compartido o
  exclusivo, 44,1–192 kHz, buffer 128–4096), presets de usuario con guardar,
  renombrar y borrar.

### Audio

WASAPI event-driven en un hilo aparte. **La latencia depende del buffer** que
elijas en Ajustes: a 48 kHz, buffer 128 = 2,7 ms por tramo y 512 = 10,7 ms; la
propia pantalla muestra el recorrido total (~21 ms con 512). No hay ASIO. La
etapa no lineal del ampli va sobremuestreada a 4×.

## Compilación

Requisitos: Visual Studio 2022 Build Tools (`cl`). Nada más.

```powershell
# App
.\_build.ps1                              # cl /utf-8 /O2 /EHsc /std:c++17, firma local
# Banco offline del DSP (sin UI ni audio)
.\native\_build_dsp.ps1
.\native\_dsp_test.exe all                # respuesta, THD, aliasing, IR, WAV, FX, coste
.\native\_dsp_test.exe extra              # hueco impreso de cada ampli (0 sin accion)
.\native\_dsp_test.exe selftest           # autotest: sale != 0 si algo falla
```

Los tres comandos valen **desde cualquier carpeta**: el banco ancla sus datos
(`_nmaref/`, `perfiles/`, `_dsp_out/`) al directorio del ejecutable, no al
directorio actual. El ejecutable sale en `native\GuitarAmp.exe`.

## Uso

1. Ejecuta `Ejecutar GuitarAmp.bat` (o `native\GuitarAmp.exe`).
2. Pulsa **AUDIO ON** en la cabecera: la app arranca con el audio apagado a
   propósito, para que decidas tú cuándo suena.
3. Elige ampli en la lista de la izquierda, monta pedales en las dos filas y
   ajusta cabina. Para cambiar de micro/descargar IR, los mandos están en la
   columna derecha.

### Cómo se ajustan los mandos

| Gesto | Qué hace |
|---|---|
| Arrastrar arriba/abajo | Ajusta el mando (1 px = 0,4 % del recorrido) |
| SHIFT + arrastrar | Ajuste fino (un cuarto) mientras arrastras |
| Rueda del ratón | 1 % del recorrido por muesca (SHIFT: un cuarto de punto) |
| Doble clic | Devuelve el mando al valor con el que se cargó |

### Teclado

| Tecla | Qué hace |
|---|---|
| ESPACIO | Enciende o apaga el audio |
| ← / → | Preset anterior / siguiente |
| ↑ / ↓ | Navega la lista de la izquierda (amplis o presets) |
| ESC | Cierra el modal abierto |

La ventana es redimensionable (teclado y ratón dejan de estar congelados a
1440×956: en un portátil de 1366×768 el diseño no cabe siquiera). El sistema de
diseño completo está en [DESIGN.md](DESIGN.md) y el estado del producto en
[PRODUCT.md](PRODUCT.md).


## Perfiles NAM (capturas neuronales)

La app nativa monta un decimotercer cabezal, **PERFIL**, que carga ficheros
`.nam` del [Neural Amp Modeler](https://www.neuralampmodeler.com/) y sustituye
el nucleo no lineal del amplificador por la captura. El resto del motor se
queda: pedales antes, tono (BASS/MIDDLE/TREBLE) despues, cabina a la salida.

- **Que se soporta**: `WaveNet` (**A1 y A2 completo**, incluido `condition_dsp`:
  el submodelo de condicion que traen las capturas A2 con mandos), `LSTM` y
  `ConvNet` (con y sin batchnorm). Lo que el formato define y este motor todavia
  no evalua (gating, `SlimmableContainer`, `Sequential`, PReLU) se **rechaza con
  un mensaje claro**: nunca se adivina ni se deja sonar mal.
- **Verificacion**: la salida coincide muestra a muestra con el core oficial
  compilado, canal a canal, con el oraculo de `_nmaref/` (diferencia medida
  3e-07 relativa en A2, 5e-06 en A1, 3e-06 en LSTM; redondeo de float32). Se
  ejecuta con `_dsp_test.exe nam2 <modelo.nam>` y forma parte del `selftest`.
  Ademas se comprueba el numero de pesos que exige la config contra el fichero:
  si no cuadra, el modelo se rechaza.
- **Niveles**: se usa `input_level_dbu`/`output_level_dbu`/`loudness` del
  fichero para que dos capturas no suenen con 20 dB de diferencia. El mando
  GAIN es un trim de +-12 dB sobre ese nivel; VOLUME es la salida.
- **Coste medido** (48 kHz, un nucleo): LSTM ~2 %, WaveNet A1 estandar (2
  arrays, 20 capas, 13.8k pesos) ~44 %. Los modelos ligeros son la
  recomendacion por defecto; la optimizacion SIMD esta pendiente.
- **Uso**: elegir el cabezal PERFIL en la lista, pulsar CARGAR PERFIL y elegir
  un `.nam`. La ruta se guarda en el `.ini` y se recarga al arrancar. La
  pantalla del frontal es la **tarjeta del perfil, en el lenguaje de TONE3000**:
  chips de tipo de equipo y de motor (`NAM` / `A2`), titulo, material con el
  autor que modele el fichero, y frecuencia a la derecha (en rojo si no es la
  de la sesion). En vacio dice cuantos `.nam` hay en la libreria (`perfiles/`).
  Cada celda de la cadena lleva su badge de tipo arriba a la derecha.

La app **no incluye ninguna captura**: la licencia de cada perfil es de su
autor. En `native/perfiles/` hay dos de ejemplo del propio core (MIT).


## Explorador TONE3000 (buscador de perfiles dentro de la app)

El cabezal PERFIL trae un boton **T3K** que abre un explorador con los **mismos
filtros que la web de TONE3000**: Amp + Cab, Amp Head, Cabinet, Pedal, Outboard,
Spaces y Experimental (los siete valores del enum `Gear` de su API: `amp-cab`,
`amp`, `cab`, `pedal`, `outboard`, `space`, `experimental`).

**Sin clave ya es util**: el panel abre con las **tendencias publicas**
(`/tones/trending`), con la foto, el titulo, el tipo de equipo, el formato, las
descargas y el autor de cada tono, y los chips filtran esa lista. Buscar y
descargar es lo que pide sesion.

Por pagina se pintan **las tarjetas que caben** (dos filas de tres) y la
busqueda pide exactamente esas: el paginador y lo que se ve cuentan lo mismo, asi
que ninguna pagina trae tonos que no haya forma de alcanzar.

- **Conexion**: OAuth 2.0 con el **flujo de dispositivo** (RFC 8628), que es el
  que su documentacion da para apps sin redireccion. Con el campo **vacio** la app
  usa la clave publicable que lleva dentro: al pulsar **CONECTAR** aparece un codigo
  y **se abre el navegador con el codigo ya rellenado**; al confirmarlo recibe el
  token y lo guarda cifrado (DPAPI, base64 con prefijo `enc:` en el `.ini`).
  Pegando con **Ctrl+V** una **clave secreta** (`t3k_cs_...`) o un **token**, entra
  directo por `Authorization: Bearer`, sin navegador. La clave **legacy**
  (`?api_key=...`) **no vale**: su API v1 pide cabecera `Authorization` y contesta
  `Missing or invalid Authorization header` (comprobado con la clave real).
- **Busqueda**: `/tones/search` con `format=nam` y `architecture=1`, filtrada
  por el chip activo. La busqueda **exige token**: sin el, el servidor devuelve
  401 (comprobado).
- **Descarga**: `/models?tone_id=` -> `model_url` con el Bearer -> el `.nam` se
  guarda en `perfiles/` y se carga directo en el cabezal PERFIL.
- **Red**: todo va en un hilo aparte (WinHTTP); la interfaz nunca se bloquea.

**Aviso de terminos**: la capa gratuita de la API permite los flujos OAuth con
`prompt` (`select_tone`, `load_tone`) y las listas acotadas (`favorited`,
`downloaded`, `created`, `trending`, `latest`). El endpoint `/tones/search`
queda **fuera** de esa capa: para publicar una integracion que lo use hay que
escribir a support@tone3000.com. Para uso propio es decision del usuario.


## Cadena de capturas del cabezal PERFIL

El cabezal PERFIL no es un solo fichero: es una **cadena de bloques**, con el
mismo planteamiento que la de TONE3000. Cada bloque es una captura con su
encendido y su nivel, y el orden se cambia moviendolo.

- **+ AÑADIR**: carga un `.nam` como bloque nuevo (o baja uno desde TONE3000).
- **T3K**: abre el explorador de TONE3000. Vive junto a `+ AÑADIR` y **delante**
  de los bloques: la cadena crece hacia la derecha y el boton no queda tapado
  por la primera captura.
- **`<` `>`**: mueve el bloque en la cadena (el audio los recorre en ese orden).
- **LED**: enciende o apaga el bloque sin perderlo.
- **`x`**: lo quita.
- La **pantalla** del frontal muestra el bloque seleccionado (nombre, material y
  frecuencia), y cada bloque lleva sus propios ajustes de nivel normalizados con
  los metadatos del fichero.

La cadena se guarda en el `.ini` (orden, ruta, encendido y nivel) y se recarga
al arrancar. Medido con el banco offline: dos bloques (WaveNet A1 + LSTM) en
serie cuestan **45,9 % de un nucleo** (x2,2 tiempo real) y apagar el segundo
bloque cambia la salida, como debe.

## Licencia

MIT - Ver archivo LICENSE para más detalles.

## Contribuciones

Las contribuciones son bienvenidas. Por favor, abre un issue o pull request.

## Créditos

- JUCE Framework (https://juce.com)
- Modelos basados en amplificadores clásicos
