---
target: UI app nativa GuitarAmp vs DISEÑO GUITAR ONE.html
total_score: 18
max_score: 40
na_heuristics: 
p0_count: 1
p1_count: 2
target_identity: "file:C:\\Users\\Usuario\\Desktop\\Nueva carpeta (7)\\native\\GuitarAmpNative.cpp"
target_fingerprint: "sha256:3da6b3491deb903425cf9c82bc6b1e5d0c1a476d3948d700f7fec27234a135cb"
target_path: "C:\\Users\\Usuario\\Desktop\\Nueva carpeta (7)\\native\\GuitarAmpNative.cpp"
timestamp: 2026-10-07T11-39-32Z
slug: native-guitarampnative-cpp
---
# Crítica de diseño — GuitarAmp (app nativa) vs DISEÑO GUITAR ONE.html

**Method: dual-agent (A: AssessmentA · B: AssessmentB)**

Evaluación de la app nativa Win32 (`native/GuitarAmpNative.cpp`, 5464 líneas, "Interfaz estilo GUITAR ONE") frente al mockup `DISEÑO GUITAR ONE.html`. Resto del repo: el plugin JUCE (`src/`) es un proyecto paralelo que no compila (falta la carpeta `JUCE/` que exige `CMakeLists.txt`) y cuya UI (3 amps, estilo mueble físico) no corresponde a este diseño; `standalone.html`/`preview.html` son mockups antiguos del plugin, no el objetivo. La restricción del usuario se respetó en todo momento: **los frontales de amps (fondos + filmstrips + perillas) quedan exactamente como están**; el arte del escenario central es correcto y está fuera de alcance.

## Puntuación de salud de diseño

| # | Heurística | Score | Problema clave |
|---|-----------|-------|-----------|
| 1 | Visibilidad del estado del sistema | 2 | Relleno de medidores ~200 px desplazado respecto a su pista (fills x=502/772/1020 vs tracks x=300/520/740); LED rojo estático siempre encendido junto a "GATE" |
| 2 | Match sistema / mundo real | 2 | "ANADIR" sin Ñ (diseño: "AÑADIR EFECTO"); píldoras "MIC: DYN" / "DIST: MID" con orden de ciclo ciego |
| 3 | Control y libertad del usuario | 1 | Caja de preset "▼" que al clicar cambia preset en silencio; lista/tabs/búsqueda inertes; sin deshacer |
| 4 | Consistencia y estándares | 2 | Tabs pintadas y muertas (AMP\|CAB, PRESETS); dos indicadores CLIP contradictorios; los `<select>` del diseño son píldoras ciclantes |
| 5 | Prevención de errores | 2 | Clics laterales sobrescriben ajustes en vivo; medidores mal etiquetados invitan a gain staging erróneo |
| 6 | Reconocimiento > recuerdo | 1 | Opciones de micro/distancia ocultas en orden de ciclo; pestaña PRESETS inalcanzable (el código ya la soporta); búsqueda muerta |
| 7 | Flexibilidad y eficiencia | 2 | SPACE/←→/ESC existen pero sin documentar; knobs con arrastre+rueda+tooltips (bien); sin navegación rápida de amps |
| 8 | Estética y diseño minimalista | 3 | Hero y paleta fuertes; muerta la mitad derecha del pedalboard con un "CLIP" flotando; choque "CABNA ACTIVA" |
| 9 | Diagnosticar/recuperar errores | 2 | MessageBox de IR con formatos aceptados (bien); controles muertos sin feedback; sin guía de primer arranque ("pulse AUDIO ON") |
| 10 | Ayuda y documentación | 1 | Sin ayuda en la app, sin pistas de atajos; tooltips solo al arrastrar knobs |
| **Total** | | **18/40** | **Pobre (45 %): la experiencia central está rota** |

Carga cognitiva: 6 de 8 checklist en fallo (filas de lista 10 visibles para 12 amps; paleta de 18 tiles; estados solo en hover; etiquetas abreviadas; clics sin efecto; **las dos filas de pedalboard codifican orden antes/después del amp sin etiquetarlo** — la única decisión que cambia el sonido más allá de cualquier knob). Solo pasan "una acción primaria por región" y "acciones reversibles".

## Veredicto de especificidad de diseño

**Auténtico en piel, genérico y parcialmente no funcional en comportamiento.** El cromo es reconociblemente este producto: el wordmark GUITAR ONE, la rejilla oscura de 3 columnas y sobre todo los frontales reales como hero reutilizados como miniaturas de lista — esa hero es la única superficie que ningún otro simulador de amp enviaría. Pero el port borró o rompió casi cada interacción firma del mockup: las tabs AMP|CAB se pintan y se sobrepintan sin cablear; las tabs MICRÓFONO|SALA desaparecieron (solo se dibuja "CABINA"); los selects etiquetados "Condenser/Dynamic/Ribbon" y "Mid/Close/Far" se convirtieron en píldoras ciegas "MIC: DYN" / "DIST: MID"; las barras "MIC 70 %" / "SALA 20 %" colapsaron en un knob MIX sin etiqueta; el paño de la cabina ya no imprime "2 x 12 OPEN"; el stepper ‹ nombre › del preset se convirtió en una caja "▼" que cicla; el campo "Buscar…" y las tabs AMPLIFICADORES|PRESETS no aceptan entrada (no existe WM_CHAR; g_ampFilter/g_searchFocus/g_leftTab se declaran y nunca se escriben). A simple vista parece una captura genérica de amp-sim, no la herramienta viva que promete el HTML: ése es el significado concreto de "no hay manera".

**Escaneo determinista:** `impeccable detect --json` sobre el mockup (el único artefacto con markup) → exit 0, 11 hallazgos, todos sobre el HTML de diseño (`dark-glow` ×8, `tiny-text` ×1, `border-accent-on-rounded` ×1, `repeating-stripes-gradient` ×1 advisory; `line: 0` en todos por minificación). Son falsos positivos para este propósito: los glows son acentos intencionados de pedales/LEDs, el 9 px es su estilo denso, la textura de rayas es el paño de la cabina. Ninguno mapea a los fallos de la app. El escáner no aplica a `GuitarAmpNative.cpp` (Win32/GDI+ puro, sin markup). **Overlay de navegador: no disponible** (superficie nativa; la extensión del relay de omp no está conectada); las capturas en disco (`_app_shot.png` = app, `_diseno_shot.png` = diseño, verificadas por contenido) sustituyen como evidencia visual.

## Impresión general

El port clavó el esqueleto (rejilla de 3 columnas, paleta, paneles, footer) pero convirtió casi cada interacción del mockup en decoración. La mayor oportunidad es única y barata: la columna izquierda ya renderiza 10 de los 12 amps con su arte real — cablear sus clics convierte una captura en un navegador. Segunda oportunidad: los medidores son el instrumento de confianza de esta app y hoy mienten (el relleno que se ve bajo "OUTPUT" es en realidad la señal de INPUT).

## Qué funciona

1. **Frontales reales como hero, reutilizados como miniaturas 70×36** (dibujo contain-fit) — identidad de producto auténtica que respeta la restricción del arte congelado; lista y hero hablan el mismo idioma visual.
2. **Profundidad operativa de la status bar por encima del diseño:** dispositivos IN/OUT, modo WASAPI, frecuencia, buffer con ms — los datos de confianza de rig que necesita un jugador en directo, más ricos que la línea del mockup.
3. **Feedback de alto riesgo bien juzgado:** el timer "PARAR m:ss" de grabación, el modal de afinador y la latencia estimada antes de "APLICAR" en ajustes son momentos Operate bien resueltos.

## Problemas prioritarios

**[P0] El navegador de amps está muerto — tabs, búsqueda y filas no aceptan input.** Las pestañas "AMPLIFICADORES"|"PRESETS", el campo "Buscar…" y cada fila (p. ej. "PANEL 800 / BRIT · MID DRIVE") se resaltan al pasar el ratón pero no reaccionan: no hay hit-testing de filas en WM_LBUTTONDOWN, no existe WM_CHAR en todo el archivo, y g_leftTab/g_ampFilter/g_searchFocus/g_favMask/g_listScroll solo se declaran (2563-2567) y leen. Con 10 filas visibles y 12 amps, los dos últimos son inalcanzables incluso a ojo; el único gesto que cambia de amp es una banda invisible de carrusel legacy bajo el arte (5103-5140). *Por qué importa:* la superficie de navegación primaria del diseño está muerta; un power user no puede hacer A/B de amps. *Fix:* hit-testing de filas + tabs en WM_LBUTTONDOWN, WM_CHAR para la búsqueda, scroll real de la lista; la pestaña PRESETS ya la soporta listFilterIdx (3863-3867). *Comando:* `/impeccable harden`.

**[P1] Los medidores mienten — rellenos ~200 px desplazados y CLIP contradictorio.** Las pistas estáticas "INPUT"/"OUTPUT"/"GATE" se dibujan en x=300/520/740 (w=160/160/85; 3752-3754) y los rellenos dinámicos en x=502/772/1020 (w=200/200/85; 4501-4502, 4509): el relleno visible bajo "OUTPUT" es la señal de INPUT. A la vez, el footer dibuja un LED rojo estático siempre encendido junto a "GATE" (3756-3757) mientras el LED real de clip (con decaimiento ~1 s) flota huérfano en (886, 797), en el hueco muerto del pedalboard (4517-4522). *Por qué importa:* medidores y CLIP son el instrumento de confianza; leer el canal equivocado produce gain staging erróneo y tomas clipeadas; el punto rojo permanente es una falsa alarma permanente. *Fix:* una única tabla de constantes que alimente tanto drawMeterTrack como drawMeterFill; borrar el LED estático y mover el dinámico a la fila del footer tras GATE (posición del diseño), conservando el decaimiento. *Comando:* `/impeccable layout`.

**[P1] Panel de cabina roto respecto al diseño — choque de texto, paño vacío y controles perdidos.** El estático "CABINA ACTIVA" (3706-3708, en CAB_LBL_Y=136) se sobrepinta con el "CAB" dinámico + nombre del IR en el mismo origen (4055-4056, 4071-4074) → "CABNA ACTIVA CLOSED" en la captura. El paño de la cabina ya no imprime el nombre activo (3714-3718) como el diseño imprime "2 x 12 OPEN". Faltan las tabs MICRÓFONO|SALA, las etiquetas sobre micro/distancia, la sección MEZCLA y el segundo slider SALA (no existen tales literales en el archivo). *Por qué importa:* una etiqueta ilegible rompe la identificación de la cabina — un control de tono de primera clase; el paño vacío elimina la lectura rápida del diseño. *Fix:* eliminar el estático (la capa dinámica ya imprime "CAB <nombre>") o bajar el dinámico a una segunda línea; imprimir el nombre en el paño; portar tabs/etiquetas/MEZCLA/SALA del mockup. *Comando:* `/impeccable polish`.

**[P2] Cabecera y footer: preset "▼" que cicla en silencio, MASTER sin dB, nombres cortados.** La caja superior dibuja "▼" junto a "05. Panel 800" pero un clic avanza de preset vía applyPreset(idx+1) (5216-5223); el diseño muestra un stepper ‹ nombre ›. La etiqueta "MASTER" (3762) se solapa con el knob (2639: centro 1290,880 r=30) y "0.0 dB" solo aparece en el tooltip de arrastre. El nombre "BLOOMING ORANGE" se corta a "BLOOMING ORANG" (rect de 164 px en 4242-4244, sin trimming). *Por qué importa:* "▼" promete un desplegable y un clic descartando ajustes en vivo es una trampa; sin dB en reposo no se puede leer el master. *Fix:* restaurar ‹ › flanqueando el nombre (zonas de clic como las flechas del footer, 5225-5236) o implementar un desplegable real; recolocar "MASTER" y "0.0 dB"; ensanchar el rect del nombre con ellipsis. *Comando:* `/impeccable polish`.

**[P2] Fantasmas del diseño y copia inconsistente.** Las tabs AMP|CAB (3594-3602) se pintan en la escena estática y las sobrepinta el chrome del título del amp — invisibles e inertes. "ANADIR" sin Ñ en slots fantasma y modal (4341, 4371) mientras la paleta sí imprime "CLIC PARA AÑADIR" / "YA ESTÁ EN LA CADENA" con Ñ. "AUDIO OFF" se parte en dos líneas dentro de su botón de 100 px (rect de texto de 74 px, 4005-4007). La status bar no muestra "GUITAR ONE v1.0.0" ni el formato del diseño. Las dos filas de pedalboard no etiquetan ANTES/DESPUÉS DEL AMP. *Por qué importa:* es la diferencia entre "copia el diseño" y "dibuja algo parecido". *Fix:* borrar las tabs fantasma (o cablearlas), unificar la Ñ, ensanchar el botón, añadir versión al status, etiquetar las filas del pedalboard. *Comando:* `/impeccable clarify`.

## Banderas de personas

- **Alex (power user)** — quiere A/B rápido de amps. Falla en: filas de "AMPLIFICADORES" (hover sí, clic nada; 4213-4216); "Buscar…" (sin WM_CHAR); pestaña "PRESETS" (g_leftTab nunca se escribe, 2563); píldoras "MIC: DYN"/"DIST: MID" (orden ciego DYN→RIBBON→COND); valor de MIX invisible hasta hover; la caja "▼" que cambia amp+fx+cab en vez de solo el amp. El único gesto útil es la banda invisible del carrusel legacy (5103-5140) — lo último que un power user descubriría.
- **Sam (dependiente de accesibilidad)** — toda la UI son píxeles GDI+ personalizados: cero árbol de accesibilidad, sin roles/nombres UIA; teclado limitado a SPACE/←→/ESC (5340-5371), sin navegación Tab; dianas de clic de 16 px (X de pedal) y 13 px (punto de toggle); "CLIP" a 7,5 pt; medidores solo por color, sin dB numérico en reposo; ventana fija 1440×956 sin maximizar (5415-5418) — sin ruta de escalado.
- **Riley (stress tester)** — clics en todo: la caja "▼" muta el rig repetidamente; filas 11-12 de la lista inalcanzables (12 amps, 10 filas, g_listScroll solo se acota, 4203-4204); rueda rápida sobre el centro cambia de amp por la banda legacy; nombres largos de dispositivo se truncan (%.40s, 4652-4657) en una línea de 22 px; el único borde manejado con elegancia es el error de IR ("No se pudo cargar el WAV…", 5020-5024).

## Observaciones menores

- La ventana está bloqueada en 1440×956 sin maximizar ni redimensionar (5415-5418): en un portátil 1366×768 la app no cabe.
- El corazón del footer alterna g_fav (5218-5221) sin efecto visible más allá de su propio color — control huérfano; las estrellas por amp del diseño son el g_favMask muerto.
- La franja roja de firma se relocó de bajo el head (border-bottom:#d22 del diseño) al borde inferior de todo el panel central (3627-3631): se lee como divisor, no como identidad del head.
- El hueco vacío del panel central (y≈440-600) aloja solo zonas de clic invisibles del carrusel legacy: espacio muerto con comportamiento oculto.
- **Contexto de proyecto:** el repo no es un repositorio git; `GuitarAmpNative.cpp` (266 KB, monolito) se parchea mediante scripts de reemplazo de texto por posición (`_batchN1`…`_batchN3a.ps1`, `IndexOf`+`Substring`) — ese flujo frágil es parte del porqué "no hay manera": cada tira de diseño se aplica a ciegas sobre un archivo de 5464 líneas. Compilar: `cl /O2 /EHsc /std:c++17 …` según el encabezado del propio archivo; el `.exe` actual (02:59) coincide con el `.cpp` (02:59).

## Preguntas provocadoras

1. Si la lista de amps, las tabs y la búsqueda están pintadas pero inalcanzables, ¿es la columna izquierda un navegador o una captura — y qué amps puede alcanzar un jugador a mitad de directo además del que congeló el preset actual?
2. El diseño vende MEZCLA como "MIC 70 %" / "SALA 20 %" con números visibles; la app lo colapsó en un knob MIX sin etiqueta — ¿se cortó SALA a propósito, y si sí, ¿por qué el port sigue dibujando el esqueleto de tabs de un panel que ya no existe?
3. Un punto rojo permanente junto a "GATE" dice "estás clipeando" antes de sonar la primera nota — ¿es una elección estética de siempre-caliente, o la falsa alarma más ruidosa de la UI?
4. La ventana está bloqueada en 1440×956 — ¿qué pasa en el portátil 1366×768 que parece vivir en todos los bares?

> **Trend para `native-guitarampnative-cpp`:** primera ejecución para este objetivo, sin tendencia aún.
> Escrito `.impeccable/critique/<archivo>`.
