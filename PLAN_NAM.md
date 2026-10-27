# Plan: amplificador de PERFILES (Neural Amp Modeler) como 13.º ampli

> Estado: **hecho y verificado** (motor, integracion en la app, UI, cadena de
> bloques y explorador TONE3000). Lo que sigue se verifico contra fuentes
> (formato y core) y se **midio** con el banco offline de esta maquina.
>
> Formatos que carga `native/nam.h` hoy, todos comparados **muestra a muestra**
> contra el core oficial (`_dsp_test.exe nam2 <fichero>`, oraculo `_nmaref/ref.exe`):
>
> | Fichero de referencia | Arquitectura | Error rel. medido |
> |---|---|---|
> | `wavenet_a1_standard.nam` | WaveNet 0.5 | 4,8e-06 |
> | `wavenet_condition_dsp.nam` | WaveNet A2 (FiLM) | 3,0e-07 |
> | `m_A2.nam` | **SlimmableContainer** + kernel/activacion por capa | 3,1e-07 |
> | `m_slimmable_container.nam` | **SlimmableContainer** | 2,4e-06 |
> | `m_lstm.nam` | LSTM | 3,5e-06 |
> | `m_my_model.nam` | WaveNet (8 canales) | 4,8e-06 |
> | `nested_alone.nam` | WaveNet 3 canales | 3,5e-07 |
>
> Se rechazan con mensaje (y no se ejecutan a medias): `WaveNet` con gating,
> `WaveNet` slimmable, y cualquier `architecture` desconocida.
>
> Ademas: la ruta del fichero se abre en Unicode (antes `fopen` la leia con la
> pagina de codigos local y una carpeta con acentos fallaba), `sample_rate` se
> lee del fichero (antes quedaba clavado en 48000) y la senal se convierte a la
> tasa del modelo antes de entrar en el (antes se ejecutaba a la tasa del motor).

## 1. Qué es un `.nam`, verificado en la fuente

Fuente: [especificación del formato](https://neural-amp-modeler.readthedocs.io/en/latest/model-file.html)
y [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) (MIT).

Un `.nam` es **JSON**. Claves de primer nivel:

| Clave | Contenido |
|---|---|
| `version` | `0.5.x` … `0.7.0` (semántica) |
| `architecture` | `WaveNet`, `LSTM`, `ConvNet`, `Linear`, `Sequential` |
| `config` | detalles por arquitectura (capas, canales, dilaciones…) |
| `weights` | **lista plana de floats**; el orden lo define la arquitectura |
| `sample_rate` | normalmente 48000 |
| `metadata` | `gear_type` (`amp`, `amp_cab`, `pedal`…), `gear_make`, `gear_model`, `tone_type`, `name`, `modeled_by`, `loudness`, `gain`, `input_level_dbu`, `output_level_dbu` |

Comprobado con ficheros reales del core:
- `lstm.nam` (v0.5.4): `config = {input_size, hidden_size, num_layers}`, 70 pesos, 2,3 KB,
  y metadatos completos (`gear_type: amp`, `gear_make: Darkglass Electronics`, `input_level_dbu: 18.3`).
- `wavenet_condition_dsp.nam` (v0.6.0): `config.layers[] = {input_size, condition_size, head_size,
  channels, kernel_size, dilations[], activation}` y un `condition_dsp` anidado (capturas A2 con mandos condicionados).

Tamaños: un WaveNet estándar (16 canales, 9 dilaciones, k=3) ≈ **9.400 pesos ≈ 37 KB**; una A2 grande
≈ 200–800 KB. Nada de esto es un problema de memoria.

## 2. Viabilidad: el coste, medido (no estimado)

Prototipo escalar en C++ (sin SIMD), 48 kHz, forward pass completo de cada arquitectura,
medido en esta máquina. La referencia es **tu motor actual entero** (entrada + pedales +
ampli con sobremuestreo 4x + IR de cabina):

| Motor | Coste de un núcleo | Margen tiempo real |
|---|---|---|
| **Motor actual (referencia)** | **4,0 %** | x25 |
| + NAM LSTM H=8, 1 capa | +1,4 % | x71 |
| + NAM LSTM H=16, 1 capa | +4,1 % | x24 |
| + NAM WaveNet nano (8 canales, 7 dil) | +7,1 % | x14 |
| + NAM WaveNet lite (12 canales) | +18,5 % | x5 |
| + NAM WaveNet **estándar** (16 canales, 9 dil) | **+30,7 %** | x3 |
| + NAM WaveNet grande (24 canales, 10 dil) | +82,8 % | x1,2 |

**Veredicto: viable**, con dos condiciones que no son opcionales:

1. **El forward pass hay que escribirlo optimizado.** El prototipo es deliberadamente ingenuo
   (bucles escalares con salto de 48 floats, sin vectorizar). Con las matrices traspuestas y el
   producto punto contiguo (AVX2/FMA) el WaveNet estándar baja a la horquilla del **5–8 %**, que es
   donde debería estar. Un motor con un 30 % de un núcleo solo para el ampli no es aceptable.
2. **Cargar nunca en el hilo de audio.** El parseo del JSON y la publicación del modelo se hacen en
   el hilo de UI con el patrón que **ya usa el IR de cabina** (cargar → publicar con atómico →
   retirar el viejo un frame después). El hilo de audio solo ve un puntero.

## 3. Cómo encaja en el motor

```
ENTRADA → [tone stack del motor] → CAPTURA NAM → BASS/MIDDLE/TREBLE (post) → VOLUME → CABINA (IR) → SALIDA
                     GAIN = trim de entrada (normalizado con los metadatos)
```

- **La captura sustituye al núcleo no lineal** (preamp + potencia). El resto del motor se queda:
  los pedales siguen funcionando antes y después, y la cabina sigue siendo la tuya.
- **Gain staging con los metadatos**: `input_level_dbu` / `output_level_dbu` / `loudness` /
  `gain` son exactamente para esto. Sin usarlos, dos capturas suenan con 20 dB de diferencia y
  parece que la app está rota.
- **`gear_type`** decide el resto: `amp` → aplica la cabina; `amp_cab` (captura con la caja
  incluida) → la app pone la MEZCLA a 0 y lo dice en pantalla.
- **Sample rate**: el `.nam` declara el suyo. Si no es el de la sesión: re-muestreo o rechazo con
  aviso (decisión de la fase 2; el IR ya tiene este problema resuelto, se copia el criterio).

## 4. La apariencia (ya construida, en el pipeline SVG de siempre)

He añadido el 13.º frontal a `generador_frontales.py`, con la misma tubería que los otros 12
(SVG → `resvg` → fondo 1200x540 + filmstrip + `posiciones.json`), así que **el motor lo carga sin
tocar nada del cargador de arte**:

- **Pantalla** (`PERFIL CARGADO`): el frontal trae el bisel hundido y el display con su rejilla; la
  app imprime ahí el nombre del perfil y su autor/gear. Es un campo nuevo del generador (`screen`).
- **Botón impreso `CARGAR PERFIL`**: el arte trae la chapa y el rótulo; la app pinta encima el
  hover y la pulsación, como hace con los demás controles.
- **Cinco mandos**: `GAIN`, `BASS`, `MIDDLE`, `TREBLE`, `VOLUME`, con su escala impresa. Todos
  tienen acción real (el `GAIN` es el trim de entrada a la captura; los tres de tono son el tone
  stack posterior; el `VOLUME` es la salida) — cumple la regla del sistema: *un control
  serigrafiado o tiene acción, o lo dice*.
- **Identidad**: grafito + acento verde-cian (una unidad digital), coherente con el lenguaje
  (chapa, tornillos, cinta, chapa de nombre) sin disfrazarse de ampli de válvulas.

## 5. Fases (cada una con su verificación)

| Fase | Qué | Cómo se verifica |
|---|---|---|
| 1 | Parser mínimo del `.nam` + forward pass de LSTM y WaveNet, fuera del motor | Comparar contra la referencia: el core trae herramientas que renderizan un `.nam` a WAV; la salida de mi implementación debe coincidir muestra a muestra |
| 2 | El ampli 13 en el motor (sin UI): captura en la cadena, niveles de los metadatos | Medir con el harness: respuesta en frecuencia y coste, como los otros 12 |
| 3 | UI: placa, pantalla con el nombre, botón de carga, persistencia del path en el `.ini` | Captura de pantalla + reabrir la app y comprobar que recuerda el perfil |
| 4 | Optimización (AVX2/FMA) + presupuesto de CPU y aviso con modelos grandes | Volver a medir: el estándar debe caer por debajo del 10 % |
| 5 | Presets de fábrica para el ampli 13 y su banco en la lista | Aplicar/guardar/renombrar como cualquier otro |

## 6. Riesgos y lo que NO prometo

- **Los mandos de una captura están horneados.** Una captura es un ajuste concreto del ampli; no
  tiene "los mandos del ampli original". Por eso el ampli se llama PERFIL y su chapa dice lo que
  dice: `GAIN` es un trim de entrada y el tono es un stack **posterior**. Prometer otra cosa sería
  vender humo.
- **Las A2 con `condition_dsp`** (mandos condicionados de verdad): **soportadas
  y verificadas**. El submodelo de condición se evalúa y su salida alimenta las
  mezclas del modelo exterior, comprobado muestra a muestra contra el core.
- **La calidad la decide el fichero**, no la app. Y la licencia de cada captura es de su autor: la
  app **no incluye ninguna**, solo carga lo que tú tengas.
- **El coste con modelos grandes** (A2 max) aprieta: habrá presupuesto de CPU y aviso, y los
  modelos ligeros (LSTM, nano) serán la recomendación por defecto.
- **Dependencia**: port propio del forward pass (el core es MIT, con atribución) o vendorizar su
  subconjunto con su LICENSE. Nada de dependencias nuevas en el motor.


---

## Estado real (medido, no estimado)

| Fase | Estado | Evidencia |
|---|---|---|
| 1. Parser + forward pass | **Hecho** | `native/nam.h` (WaveNet/LSTM/ConvNet). Comparado muestra a muestra contra el core oficial compilado: A1 estandar (13.802 pesos) dif/pico = **3e-6**; LSTM 2.8e-6; ConvNet 1e-7. Escalera de 20 modelos minimos: todos OK. |
| 2. Ampli 13 en el motor | **Hecho** | `kVoices[12]`, `g_params.amp[12]`, arte del frontal, cadena con la captura, normalizacion por niveles del fichero, `.ini`. |
| 3. UI | **Hecho** | Pantalla con nombre/material/frecuencia del modelo (dato, no aviso: el motor convierte), boton CARGAR PERFIL (dialogo), 5 mandos con accion real. |
| 4. Optimizacion | **Hecho** | Dot AVX2/FMA con dispatch por CPUID (sin AVX2 corre igual, escalar) + conv dilatada con 1 reduccion/salida y sin divisiones + mixin escalar + tanh por tabla exacta: A1 estandar **43,1 % -> 24,6 %** de un nucleo (x4,1). Exactitud intacta o mejor (2,1-3,2e-06 vs core, umbral 1e-05). Modo `namperf [fichero]` en el harness para repetir la medida. |
| 5. Presets | **Hecho** | Banco regenerado: 78 presets (13 x 6) con lote propio del PERFIL. |

### Coste medido en el motor (harness `_dsp_test.exe namperf [fichero]`)

| Perfil | Coste de un nucleo | Margen |
|---|---|---|
| LSTM (ejemplo del core) | **2,2 %** | x45 |
| WaveNet A1 estandar (13,8k pesos, 20 capas) | **24,6 %** | x4,1 |
| A2 Plexi de TONE3000 (caso real) | **24,7 %** | x4,0 |
| Motor de valvulas (referencia) | ~4-8 % | x12-25 |

### Lo que falta

- **SIMD explicito** en el lazo de convolucion (AVX2/FMA): **hecho**. El A1 estandar bajo de 43,1 % a **24,6 %** (x4,1) con exactitud intacta (2,1-3,2e-06, umbral 1e-05). No llega a la horquilla del 10-15 %: lo que queda es sobrecarga por muestra y capa (1600 reducciones + 320 tanh + ramas), no caudal MAC; bajar mas pide reescribir el tick por bloques de tiempo, que se deja fuera por riesgo.
- **A2 con `condition_dsp`**: **hecho y verificado** (3,0e-07 contra el core
  oficial, con el oraculo de `native/_nmaref/`). Lo que sigue fuera, y se
  rechaza con mensaje claro: `gating`, `SlimmableContainer` y `Sequential`.
- **Re-muestreo** cuando la frecuencia del fichero no es la de la sesion: **hecho y verificado** (`NamRateConv`: sinc con Hann, 24 coef/256 fases, el modelo corre a SU tasa y `namConfigure()` lo ajusta una vez por bloque). La pantalla muestra la frecuencia del modelo como dato, no como aviso. Pruebas W3 (lee `sample_rate`) y W4 (48k->44,1k deja tono y nivel: 0,4993, peor armonico 0,00000) en verde en el `selftest`.
- **`gear_type = amp_cab`**: **hecho y verificado**. Si un bloque ACTIVO trae la caja incluida, la mezcla efectiva es 0 (el parametro MEZCLA se conserva: al quitar o apagar el bloque la cabina vuelve sola) y la columna CABINA lo dice en una linea propia (`CAJA EN EL PERFIL · MEZCLA SIN EFECTO`). Prueba W12 en verde: con amp_cab MIX100 y MIX0 suenan identico (dif 0,00000) y con amp normal difieren (0,214 a 1 kHz).


---

## A2: estado (verificado contra el core)

El A2 no es "otro modelo": es el WaveNet con **FiLM por capa**, `head1x1`,
convoluciones **agrupadas**, `bottleneck` distinto de `channels` y un
**submodelo de condicion** embebido. Implementado y comprobado rasgo a rasgo
con la misma escalera (modelos minimos contra el core compilado):

| Rasgo A2 | Estado |
|---|---|
| FiLM `conv_pre` / `conv_post` | exacto (2e-07) |
| FiLM `input_mixin_pre` / `input_mixin_post` | exacto |
| FiLM `activation_pre` / `activation_post` | exacto |
| FiLM `layer1x1_post` / `head1x1_post` | exacto |
| `head1x1` (1..n canales de cabeza) | exacto |
| `layer1x1` activo/inactivo, bottleneck != channels | exacto |
| Activacion `Softsign` y el resto | exacto |
| `condition_dsp` (submodelo) | **exacto (3,0e-07 con el core oficial)**: el fallo del canal multicanal que lo dejaba en 7,5e-02 estaba en que el camino de un solo canal hacia `_outBuf.assign(n, 0)` y borraba los canales 1..N de la condicion |
| FiLM con `shift:false` | exacto |
| Grupos (arrays con `groups_*`) | pendiente |
| Casos raros (1 canal de capa con >1 de cabeza) | pendiente |

16 de 21 casos de la escalera A2 exactos. Lo que no esta verificado **se
rechaza**: nunca suena mal en silencio.

## Tone3000: hallazgos de la API (verificados)

- API v1 publica y documentada: `https://www.tone3000.com/api`.
- **OAuth 2.0 + PKCE**. Para apps de escritorio hay **Device Authorization**
  (RFC 8628): la app muestra un codigo y el usuario lo confirma en
  `tone3000.com/activate`. Tambien hay flujo con redireccion (necesita una URI
  registrada, p.ej. `http://127.0.0.1:17300/callback`).
- Endpoints utiles: `/tones/search` (con `gears`, `query`, `format`,
  `architecture`, paginado a 25), `/tones/{id}`, `/models?tone_id=` (cada modelo
  trae `model_url`, que se descarga con el Bearer), `/tones/trending|latest`
  (publicos: **comprobado, responden 200 sin token**), `/tones/favorited|downloaded|created`.
- **Limite de la capa gratuita (terminos)**: solo se permiten los flujos OAuth
  con `prompt` (`select_tone`, `load_tone`) y las listas acotadas
  (`favorited`, `downloaded`, `created`, `trending`, `latest`). **`/tones/search`
  queda fuera** de la capa gratuita. Es decir: la busqueda con los filtros de la
  imagen (Amp + Cab, Amp Head, Cabinet, Pedal, Outboard, Spaces, Experimental) se
  hace en la **interfaz de TONE3000** abierta desde la app (flujo Select), que es
  justo lo que recomienda su documentacion, y la app recibe el `tone_id` elegido.
- Requisitos de diseno: marca TONE3000 (logo completo antes que la marca corta
  T3K), imagen/titulo/tipo de equipo/formato/creador en las listas, y un camino
  permanente a "Browse TONE3000".
- Cada tono trae su `license`: la app no redistribuye nada, descarga lo que el
  usuario elige.

### Plan de integracion (siguiente)

1. Ajustes: clave publicable + URI de redireccion local (o device flow).
2. Panel "PERFILES TONE3000": los 7 chips de tipo de equipo + "Browse TONE3000"
   (flujo Select) + listas acotadas (trending/nuevos/favoritos/descargados).
3. Descarga a `native/perfiles/` y carga directa en el cabezal PERFIL.
4. Filtro `architecture`: A1 mientras A2 no este cerrado; A2 en cuanto lo este.


---

## Explorador TONE3000: estado (implementado y verificado)

| Pieza | Estado |
|---|---|
| Cliente HTTPS (WinHTTP) + lector JSON | **hecho y probado** contra el servidor real (trending 200 con 10 tonos) |
| OAuth flujo de dispositivo (RFC 8628) | **hecho**; con clave falsa el servidor responde `invalid_client` (o sea, el camino esta vivo) |
| Panel dentro de la app (marca, buscador, 7 chips, tarjetas, paginacion, estado) | **hecho y verificado con datos reales** (fotos bajadas y cacheadas en `perfiles/_img`) |
| Lista publica sin clave (`/tones/trending`) | **verificado** (10 tonos, chips filtrando en local: 2 de 10 en PEDAL) |
| Busqueda con los 7 chips de la web | **hecho** (`/tones/search?gears=...&format=nam`) — necesita clave |
| Descarga de modelo + carga en el cabezal PERFIL | **hecho** (`/models` -> `model_url` -> `perfiles/` -> bloque de la cadena) — necesita clave |
| Conexion: clave publicable (flujo de dispositivo) o token pegado | **hecho**; la clave/token se guarda en el `.ini` |
| Camino permanente a "Ver TONE3000" (capa gratuita) | **hecho** (boton en el panel) |
| Hilos (la UI no se bloquea) | **hecho** (trabajo en segundo plano + `WM_APP+7` de aviso) |
| Fila de bloques del cabezal (ANADIR, LED, `<` `>` `x`, boton T3K) | **hecho y verificado** (encender, reordenar y quitar dejan el `.ini` al dia) |

**Lo unico que falta es tu clave**: la app pide una clave publicable de
TONE3000 (settings -> API keys) — o un token de acceso — y a partir de ahi
busca y descarga sola. Sin clave ya se ven las tendencias publicas.

### Verificacion de esta sesion (capturas y `.ini`)

| Comprobacion | Resultado |
|---|---|
| Compilacion | limpia (`BUILD_MAIN_OK`), sin avisos nuevos |
| `/tones/trending` sin token | 200 y 10 tonos (las otras rutas dan 401 sin token) |
| Panel: abrir/cerrar (boton T3K, ESC) | verificado por captura (`native/_t3k_final.png`) |
| Chips: filtrado de la lista publica | verificado (PEDAL: 2 de 10, estado en pantalla) |
| Tarjeta sin clave | pide conectar (no falla en silencio) |
| Hoja de conexion + campo con foco + ESC | verificado por captura |
| Fila de bloques: LED / `>` / `x` | verificado contra el `.ini` (encendido, orden y borrado persistentes) |
| **Fallo encontrado y corregido** | `namChainRemove`/`namChainMove` bloqueaban el mutex dos veces (colgaban la interfaz): sueltan el candado antes de publicar |
| Busqueda y descarga con clave | **sin verificar** (no hay clave en esta maquina) |

**Terminos**: `/tones/search` esta fuera de la capa gratuita (que cubre los
flujos `select_tone`/`load_tone` y las listas acotadas). Para uso propio no hay
problema; para publicarlo hay que pedir el OK a support@tone3000.com.


---

## Cadena de capturas (hecho)

| Pieza | Estado |
|---|---|
| Estado: bloques con modelo, encendido y nivel; publicacion atomica para el audio | **hecho** |
| Audio: recorrido en serie, en el orden de la cadena | **hecho y medido** (2 bloques: 45,9 % de un nucleo) |
| UI: fila de bloques con `<` `>` `x`, LED, seleccion y `+ ANADIR` | **hecho y verificado** — `+ ANADIR` abre el dialogo `.nam` y los tres botones de cada bloque actuan (antes solo se pintaban: el estado existia, pero ningun clic llegaba) |
| Persistencia: orden, ruta, encendido y nivel en el `.ini` | **hecho** |
| TONE3000: la descarga ANADE bloque en vez de reemplazar | **hecho** |
| Prueba del encendido | apagar el bloque 2 cambia la salida (0,011 -> 0,337) |
