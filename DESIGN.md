---
name: RACK
description: Sistema visual de GuitarAmp (app nativa Win32 + GDI+): una torre de senal donde cada unidad es un modulo de rack, chrome plano y preciso, un solo acento ambar para lo vivo, y el arte real de los amplificadores como unico color grande.
colors:
  primary: "#ffa51f"
  ink: "#f2f0ea"
  ink-muted: "#9c9890"
  ink-faint: "#6e6a63"
  surface-top: "#1f1f24"
  surface-bottom: "#151519"
  surface-line: "#2c2c33"
  well: "#070709"
  well-line: "#222228"
  steel: "#393942"
  steel-dark: "#18181c"
  hot: "#ff454f"
  warn: "#d95c50"
  canvas: "#0a0a0c"
typography:
  unit: "pt (GDI+ construye las fuentes en puntos; 1pt = 1.333px a 96 dpi)"
  display: "Segoe UI Bold 21pt (28px em), tracked +4px"
  title: "Segoe UI Bold 12.5pt (17px em)"
  body: "Segoe UI Regular 10-11pt (13-15px em)"
  label: "Segoe UI Bold 7-9pt (9-12px em), uppercase, tracked +0.4 to +1.6px"
  numeric: "Segoe UI Bold, tabular figures for dB and percent"
rounded:
  plate: "7px"
  unit: "7px"
  module: "6px"
  well: "6-8px"
  control: "5px"
  pill: "7px"
spacing:
  margin: "24px"
  gutter: "16px"
  unit-gap: "16px"
  unit-header: "26px"
  panel-pad: "14px"
components:
  unit: "chapa con banda de titulo de 26px, cuatro tornillos y filete superior de 1px"
  module: "112x108, cinta de tipo de 2px, LED de estado, dos perillas de 12px con etiqueta, interruptor de 52x14"
  meter: "pista de 9px con relleno continuo en tres tramos (marfil, ambar, rojo)"
  bridge: "pie con preset, trim INPUT, tres medidores, CLIP y MASTER con su lectura en dB"
---

# Design System: GuitarAmp — RACK

## Overview

**Creative North Star: "La torre de senal"**

La ventana entera es un rack de estudio. No hay columnas sueltas: hay una torre
donde el sonido entra por arriba (estante **antes del ampli**), pasa por el
cabezal, sale por el estante **despues del ampli** y termina en la banda de
cadena, que resume el recorrido y guarda el afinador. El orden del sonido se ve
sin etiquetarlo, y cada unidad se lee montada —con su banda de titulo, sus
tornillos y su filete— en vez de pegada al fondo.

El color grande lo pone el equipo: los frontales reales de los amplificadores,
la tela de la cabina y las caras de los pedales. El chrome es grafito plano y
un unico acento ambar que solo aparece en lo que suena o en lo que se puede
tocar. Esta mezcla —mundo de rack, material de gear real— es la posicion del
producto: un simulador que se lee como hardware sin dibujar hardware falso.

**Key Characteristics:**
- Torre vertical de tres unidades + banda de cadena: el rack es la navegacion.
- Superficies planas: dos tonos y un filete de 1px. Sin grano, sin vineta, sin halos.
- Un solo acento (`#ffa51f`) para valor activo, LED encendido, aguja, seleccion y foco.
- El frontal del ampli es el hero y ocupa el ancho completo de la torre.
- Cada parametro es alcanzable y cada mando dice lo que la chapa imprime.

## Colors

Paleta de sala de control: grafito calido, marfil de instrumento y un ambar de
filamento. Ningun color decorativo.

### Primary
- **Filamento** (`#ffa51f`): el unico acento. LED de estado encendido, aguja de
  medidor en su tramo alto, valor activo de un control, seleccion de fila,
  foco de teclado, marca de la etapa del ampli en la cadena y la firma del pie.
- **Filamento claro** (`#ffe0a0`): especular del acento (punta de aguja, gloss).
- **Techo** (`#ff454f`): solo clip y techo del medidor. Nunca como adorno.

### Neutral
- **Marfil** (`#f2f0ea`): texto principal, valores, nombres de unidad.
- **Marfil medio** (`#9c9890`): etiquetas de seccion y texto secundario (contraste >= 4.5:1 sobre las superficies).
- **Marfil bajo** (`#6e6a63`): texto terciario, numeracion, marcas de escala.
- **Grafito** (`#1f1f24` -> `#151519`): cuerpo de toda placa (dos tonos, 90 grados).
- **Filete** (`#2c2c33`): borde de 1px de las placas; **hueco** (`#070709`) y su
  filete (`#222228`) para lo hundido (listas, pistas, visualizador).
- **Acero** (`#393942` / `#18181c`): tornillos, asas y carriles del rack.
- **Lienzo** (`#0a0a0c`): fondo de la ventana, con una lavada superior al 6%.

### Named Rules
**La Regla del Unico Vivo.** El ambar no decora: senala lo que suena o lo que
se puede tocar. Si un elemento no es estado ni control, no lleva acento.

**La Regla del Color del Equipo.** Ningun color de interfaz identifica a un
amplificador: su identidad es su frontal real. La cinta de tipo de un modulo solo
se enciende en ambar cuando el pedal empuja el ampli (ganancia); el resto de los
17 tipos va en el neutro del chrome (`kTxtMd`), y la marca de la cabina y el
corazon de favorito siguen la misma regla. El chrome es monocromo.

## Typography

**Display / Body / Label:** Segoe UI (Regular, Bold). Una sola familia en tres
tamanos; la jerarquia la llevan el peso, las versalitas y el tracking, no una
segunda fuente.

**Character:** Instrumento, no folleto: rotulos en versalitas espaciadas como
la serigrafia de una chapa, cifras tabulares para niveles, y nada de texto
grande compitiendo con el frontal.

### Hierarchy
Las medidas van en PUNTOS porque GDI+ interpreta asi el tamano de `Gdiplus::Font`;
entre parentesis, el em resultante a 96 dpi.
- **Display** (Bold 21pt = 28px em, tracking +4px): wordmark GUITAR ONE.
- **Title** (Bold 12.5pt = 17px em): titulo de unidad en la banda del cabezal.
- **Body** (Regular 10-11pt = 13-15px em): nombres de preset, pills, acciones.
- **Label** (Bold 7-9pt = 9-12px em, versalitas, tracking +0.4 a +1.6px): titulos de
  unidad, etiquetas de perilla, nombres de modulo, cabeceras de instrumento.
- **Numeric** (Bold, cifras tabulares): `66 dB` del MASTER, `100 %` de MEZCLA,
  pico en dB del visualizador.

**El tracking declarado es el que se pinta.** El texto espaciado se dibuja glifo a
glifo, y `MeasureString` suma un relleno de maquetacion de **1/3 de em a cada
llamada** (4px a 9pt, medido). Sumarlo por caracter inflaba el espaciado real a ~3x
lo previsto y hacia que los rotulos cayeran al cuerpo minimo: por eso `glyphAdvance`
mide el relleno una vez por tamano (`spacedPad`) y lo resta.

### Named Rules
**La Regla de un Solo Modal.** Solo hay una superficie modal abierta: abrir
AJUSTES, AFINADOR, RENOMBRAR, AÑADIR o el explorador TONE3000 cierra las demas
(`modalCloseAll`). Antes cada apertura solo ponia su propio flag y podian
coexistir dos modales a la vez: el de abajo seguia robando el clic (el orden de
pintado y el de captura no coincidian) mientras el de arriba lo tapaba.
**La Regla del Rotulo Encajado.** Todo texto que se dibuja con `drawSpacedFit` /
`drawSpacedFitC` cabe en la caja que se le da: primero se encoge hasta su cuerpo
minimo y, si aun asi no cabe, se corta con puntos suspensivos por la cola. Nunca
invade lo de al lado. Antes el ajuste tenia un suelo y se rendia **en silencio**:
el rotulo del boton de conectar con TONE3000 llevaba la URL entera (~55 caracteres
en 300px) y se salia de su caja encima de CANCELAR. Un rotulo que no quepa se
acorta; no se deja crecer.
**La Regla de la Etiqueta Impresa.** Un mando montado en un hueco serigrafiado
se llama **como la chapa que tiene debajo**, aunque el parametro interno se
llame de otra forma. La interfaz nunca contradice al frontal.

## Layout

Rejilla de tres columnas sobre **1440x956 de area cliente de referencia**: navegador
(300px) / torre (720px) / cabina (340px), margen 24px y canal 16px.

**La ventana es redimensionable y toda la interfaz se escala.** Todo el codigo
(dibujo e hit-test) trabaja en coordenadas de DISENO: una transformacion de GDI+
aplica la escala a formas, plumas, sombras y tipografia a la vez, y la entrada del
raton se convierte con `uiX`/`uiY`. Asi ni un literal pixel suelto se olvida de
escalar y dibujo y raton no pueden desalinearse. El marco se calcula con
`AdjustWindowRectEx`, que ya suma `SM_CXPADDEDBORDER` (4px por lado en Windows
10/11); sin contarlo el area cliente quedaba 8px estrecha y el margen derecho caia
a 13px. La ventana arranca en el tamaño que cabe en el area de trabajo y no baja
de 1120x760, asi que en un portatil 1366x768 se ve entera.

La torre se apila en vertical con 12px entre unidades: estante ANTES (148px),
cabezal (366px, con el frontal a 708x318), estante DESPUES (148px) y banda de
cadena (46px). El pie (846..916) es el bridge: preset, trim de entrada, tres
medidores, clip y master. La linea de estado cierra con la firma del producto.

Los estantes admiten 6 modulos de 112x108 con paso de 116px y crecen desde la
izquierda en orden de senal. Se dibujan **solo las plazas ocupadas mas la libre**:
seis chapas vacias eran la mayor zona muerta de la pantalla. La lista del navegador
usa filas de 52px (12 amplis caben sin scroll) y su leyenda de atajos cierra el panel.

## Elevation & Depth

Hibrido contenido: cada placa lleva una sombra de contacto (capas concentricas
hacia abajo-derecha) para apoyarse en el lienzo, y nada mas. No hay halos, ni
vinetas, ni degradados de mas de dos paradas: la profundidad la dan la
diferencia de valor entre placa y hueco, el filete claro superior de 1px y la
luz de la escena (una sola lavada desde arriba).

### Shadow Vocabulary
- **Contacto de placa** (`softShadow(r=7, alfa=90)`): unidades, modulos, botones y modales.
- **Contacto de objeto** (`softShadow(r=5, alfa=140)`): el frontal del ampli dentro de su unidad.
- **Velo de pulsacion** (negro al 27%): feedback de un control pulsado.

### Named Rules
**La Regla de Plano por Defecto.** Una superficie en reposo no tiene degradado
decorativo ni bisel: dos tonos y un filete. Si algo brilla, es porque esta vivo.

## Shapes

Radio unico de sistema: 7px en placas y unidades, 6px en modulos, 5px en
controles y 8px en huecos. La silueta recurrente es la **chapa de rack**: banda
de titulo arriba, cuatro tornillos en las esquinas y un filete que la cierra.
Los controles son circulares (perillas, LEDs) o pastillas (pills, interruptores).

## Components

### Buttons
- **Shape:** pastilla de 5-7px de radio; boton de hardware con cuerpo en dos tonos.
- **Primary:** cuerpo `#26262c` -> `#16161a` con LED ambar a la izquierda cuando esta activo.
- **Hover / Focus:** cuerpo `#33333a`, borde de acento, texto a marfil pleno.
- **Secondary:** boton plano de placa con borde de filete; el texto pasa a marfil al pasar por encima.
- **Estado de audio (`AUDIO ON` / `AUDIO OFF`):** es el unico boton con dos
  estados vivos, y se distinguen por **color**, no por el texto:
  **ambar** `#ffa51f` cuando la app suena y **rojo de aviso** `#d95c50` cuando
  esta apagada. En los dos casos el cuerpo va tenido al 30% del color (42% al
  pasar por encima), el borde es de 1px del mismo tono y el piloto esta
  encendido, asi que el boton nunca parece apagado del todo.
  El rojo de aviso es un escalon mas apagado que el de clip (`#ff454f`): el
  ambar no vale para el estado apagado porque significa "esto suena", justo lo
  contrario. La pastilla de la linea de estado usa el **mismo criterio**
  (ambar = suena, rojo = no suena), para que la cabecera y el pie no digan cosas
  distintas del mismo estado.

### Chips (etapas de la cadena)
- **Style:** texto medido y ajustado al ancho de su celda, marca vertical de 1px
  (3x14px en la etapa del ampli, 1x8px en el resto).
- **State:** la etapa del ampli va en ambar; las demas en marfil medio.

### Selectores de valor (‹ valor ›)
- **Forma:** pastilla del sistema con la etiqueta en versalitas a la izquierda
  (`CAB`, `MIC`, `DIST`), el valor en marfil a continuacion y una flecha de
  1,8-2px en cada extremo. Un pill que solo cicla esconde las otras opciones y
  obliga a probar a ciegas; con dos flechas se ve de que lado queda cada valor y
  se puede volver.
- **Zona:** una sola por fila. La mitad izquierda retrocede y la derecha avanza;
  el clic decide el sentido, y la flecha bajo el raton se enciende en ambar
  (hoverKey devuelve una clave por mitad para que el repintado no se pierda).
- **Valor externo:** un IR de usuario muestra el **nombre del fichero** en ambar
  (recortado con puntos suspensivos), no la etiqueta `USER IR`.
- **Accion frente a selector:** `CARGAR IR` usa la misma pastilla pero sin flechas
  y con el texto en marfil medio; nunca se confunde con un selector.

### Modulos del rack (pedales)
- **Chapa:** `panelBase` de 112x108 con radio 6px, cinta de tipo de 2px arriba,
  LED de estado, dos perillas de 12px con su etiqueta e interruptor de 52x14.
- **Cache:** la plantilla de cada tipo se rasteriza UNA vez con 6px de margen y se
  blitea entera. Su margen **se pinta con el tono que el estante tiene en esa
  franja** (interpolado de `kSurfA`/`kSurfB`): con el fondo en negro, cada pedal
  aparecia con un marco opaco que rompia el metal del estante.

### Cards / Containers
- **Corner Style:** 7px (unidad), 6px (modulo).
- **Background:** placa `#1f1f24` -> `#151519`; banda de titulo `#111115`.
- **Shadow Strategy:** solo contacto (`softShadow`), nunca elevacion flotante.
- **Border:** filete `#2c2c33` de 1px y un filo claro al 9% arriba.
- **Internal Padding:** 8-14px; la banda de titulo mide 26px.

### Inputs / Fields
- **Style:** hueco `#070709` con borde `#222228`, radio 6px.
- **Focus:** borde de acento y cursor de 1.2px del mismo ambar.
- **Empty:** placeholder en marfil bajo, nunca en gris generico.

### Navigation
- **Style:** pestanas de texto en versalitas con filete ambar de 64x2px bajo la
  activa; filas de lista de 44px con miniatura del frontal enmarcada en un hueco.
- **States:** hover con placa `#26262b`; activa con relleno degradado, filete
  ambar, barra de seleccion a la izquierda y filo de luz arriba.

### Cabina (firma)
- **Mueble:** cara superior con su filete y canto de luz, cuerpo de tolex un
  escalon por encima del panel (`#26262b`) con filo claro arriba, cuatro
  esquineras con sombra interior y filo, y baffle hundido (sombra arriba, luz
  abajo) enmarcado por un filete de 2px.
- **Tela:** el baffle es un pozo con tejido de 3px (hebras verticales y
  horizontales al 6-9% de alfa) y una trama de 6px por encima de TODO, incluidos
  los conos: es lo que hace leer los altavoces **detras** de la rejilla y no
  pegados sobre ella. Luz de arriba en degradado de dos paradas.
- **Conos:** se dibujan antes de la tela y el radio se DERIVA de la geometria del
  baffle y del diametro real del altavoz (`2x10` dibuja conos de 10", mas
  pequenos que los de un `2x12`). Nunca se solapan: con dos conos el radio sale
  del ancho disponible, no de la altura. Aro de cesta con filo de luz, garganta
  oscura, campana y polvo con su arco de brillo.
- **Micro:** capsula de acero con rejilla oscura, halo que la separa del cono y
  brazo que entra desde el borde derecho del mueble (el pie no se ve desde una
  vista frontal: no se inventa una base flotando en la tela). La distancia
  elegida mueve el micro sobre el baffle.
- **Sin linea de estado encima:** el previo empieza bajo la pestana CABINA
  (300x308) y el valor activo vive en el selector `‹ CAB ... ›`. Repetir el
  nombre del cab dos veces era el unico texto sobrante de la columna.

### Mapa de senal (banda de la cadena)
- **Estructura fija:** seis etapas siempre presentes — ENTRADA, PRE, cabezal,
  cabina, POST, SALIDA — para que el mapa no salte al anadir o quitar pedales.
- **Nodos:** anillo en los dos extremos (la senal entra y sale), **una marca por
  pedal** en su color de tipo dentro de PRE y POST (una raya apagada si la fila
  esta vacia), un tick para la cabina y un **rombo ambar** para el cabezal, que
  es el unico nodo vivo del recorrido.
- **Rotulos:** serigrafia medida y ajustada al ancho de su celda, encima de su
  nodo; el del cabezal en ambar.

### Medidores y visualizador (firma)
- **Meter:** pista de 9px (hueco) con relleno continuo en tres tramos: marfil
  hasta el 72%, ambar hasta el 90%, techo rojo; retencion de pico como aguja de 1px.
- **Scope SALIDA:** visualizador con graticula visible (alfa 64) y linea base
  reforzada (alfa 96), trazo ambar con glow, linea plana cuando no hay senal y
  **pico real en dB** en su cabecera: `SALIDA` a la izquierda y la cifra
  tabular a la derecha (`-14.2 dB`, ambar por encima de -6 dBFS, `— dB` en
  silencio). Un visualizador sin numero es un adorno.

### Parametros que el frontal no imprime
- El motor da seis parametros por amplificador; hay chapas que solo serigrafian
  tres (AMBER TWEED). Esos parametros **no** van al chrome: se integran **en la
  propia chapa**, en los huecos que dejan los mandos impresos, a su misma altura,
  con un mando menor (~80px de arte frente a 112px) y su etiqueta en la voz de la
  serigrafia. La fila lee como seis mandos del ampli, no como controles colgados.
- La regla general: un parametro sin hueco impreso vive con el equipo al que
  pertenece; el chrome no es una bandeja de mandos sueltos.

### Controles impresos que el motor no tiene
Un control serigrafiado en la chapa **o tiene accion, o lo dice**. Nunca un mando
que se puede girar y no hace nada.
- **Con accion (control extra del ampli):** el motor reserva un septimo parametro
  por amplificador y **el nombre impreso decide que hace**: `RESONANCE` -> un pico
  de graves en la carga de la potencia (95 Hz, +-8 dB, medido 10,1/17,7/26,3 dB a
  105 Hz con el mando en 0/50/100); `OUTPUT` / `VOLUME` / `MASTER` -> un recorte de
  nivel de +-10 dB sobre la salida del ampli. Neutro en 50, asi que los presets de
  fabrica suenan exactamente igual que antes.
- **Sin accion:** el mando se pinta **aparcado** (cuerpo apagado, sin puntero ni
  arrastre) y su tooltip dice `NO MODELADO` con el nombre impreso. Aparcado y
  mudo es honesto; vivo y mudo es una mentira. Hoy **no queda ninguno**: el
  harness lo comprueba (`_dsp_test.exe extra` imprime "huecos sin accion").

El motor reserva **dos** controles extra por amplificador y su funcion la decide
el nombre impreso. Medido en el harness (mando en 0 / 50 / 100):

| Nombre impreso | Funcion | Medida |
|---|---|---|
| `RESONANCE`, `DEEP` | Pico de graves en la carga de la potencia (95 Hz) | dB a 105 Hz: `10,1 → 26,3` |
| `OUTPUT`, `VOLUME`, `MASTER` | Recorte de nivel a la salida (±10 dB) | dB a 105 Hz: `9,9 → 27,7` |
| `INTENSITY` | Profundidad de tremolo | modulacion: `1,7% → 201,5%` (260 ms) |
| `SPEED` | Velocidad de tremolo (0,6–7,6 Hz) | periodo de la envolvente |
| `RATE` | Velocidad de chorus (0,15–5 Hz) | `1,7% → 15,0%` |
| `DEPTH` | Profundidad de chorus | `1,6% → 25,2%` |
| `COMP` | Compresion a la salida del cabezal | pendiente: `-0,44 → -0,78` |

Los neutros son **por funcion** (profundidad = 0 apagado; el resto = 50), asi que
los presets de fabrica suenan igual que antes. Mover la velocidad de un modulador
enciende una cantidad minima, para que ese mando no parezca muerto.

## Interaccion

Un mando se ajusta como en el hardware que imita, y con los mismos gestos que
Guitar Rig o Amplitube. No hay gesto inventado:

- **Arrastrar** arriba/abajo sobre el mando (1 px = 0,4% del recorrido, y el
  recorte fino con **SHIFT** mientras se arrastra). El cursor sobre un mando es
  `IDC_SIZENS`; sobre el slider MEZCLA, `IDC_SIZEWE`: el cursor dice donde se
  puede tocar antes de que el raton llegue.
- **Rueda** sobre el mando: un 1% del recorrido por muesca; con **SHIFT**, un
  cuarto de punto. Sobre el slider MEZCLA, lo mismo.
- **Doble clic** sobre un mando: vuelve al valor con el que se cargo (el estado
  del `.ini` o del preset aplicado). El valor de referencia se guarda **por
  parametro**, no por posicion: los mandos del frontal cambian de orden y de
  numero con cada ampli y un vector paralelo restauraria el valor de otro mando.
- **Selectores de la cabina:** clic en la mitad izquierda de la pastilla para el
  valor anterior y en la derecha para el siguiente; la flecha bajo el raton se
  enciende. `CAB` recorre los seis modelos y, si hay un IR cargado, el USER IR;
  `MIC` recorre DYN / RIBBON / COND y `DIST` NEAR / MID / FAR.
- **Lectura de valor** (`drawValuePop`): hueco `#070709`, filete de 1,5px del
  acento y cifra ambar en negrita de 11pt, una sola receta para el mando y para
  el slider. La caja **nunca sale de la ventana** (se limita a los 12px de
  margen, y los mandos altos la muestran debajo): antes el mando no limitaba
  arriba y la burbuja se salia con los del cabezal.
- **Teclado:** ESPACIO silencia el canal, ←/→ cambian de preset, ↑/↓ recorren la
  lista de la izquierda (amplis o presets, con el resaltado de fila como foco
  visible y auto-scroll) y ESC cierra el modal abierto.

## Movimiento

La interfaz **solo** se mueve cuando informa: la cascada de encendido de la torre
y los medidores. La balistica de los medidores es de VU y esta expresada en
**tiempo**, no por bloque: ataque de ~10 ms (el transitorio se ve) y caida de
~250 ms (la aguja se lee y no parpadea). La caida fija por buffer que habia
antes hacia que la misma nota cayera mas rapido con buffer pequeno que con
buffer grande, asi que el medido bailaba distinto en cada maquina.

## Do's and Don'ts

### Do:
- **Do** montar cada unidad como chapa de rack: banda de titulo, tornillos, filete.
- **Do** llamar a un mando como la chapa que tiene debajo (etiqueta impresa).
- **Do** dar a cada parametro un control alcanzable; si el frontal no lo imprime,
  va como mando pequeno en la banda del cabezal con su nombre.
- **Do** pintar un hueco sin parametro como mando **aparcado** (apagado, sin puntero).
- **Do** mantener el ambar por debajo del 10% de la pantalla.

### Don't:
- **Don't** usar degradados de mas de dos paradas, halos, grano ni vinetas.
- **Don't** dibujar un control que no responda: o es estado, o es mando, o no esta.
- **Don't** repetir la misma etiqueta dos veces sobre la misma superficie.
- **Don't** dejar texto que se salga de su celda: se mide y se ajusta.
- **Don't** introducir un segundo acento de color: el ambar es el unico vivo.
