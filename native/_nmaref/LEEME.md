# _nmaref — oráculo de verificación del motor NAM

Aquí vive el **renderizador de referencia** con el que se comprueba que el motor
propio (`native/nam.h`) calcula lo mismo que el core oficial de
[NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) (MIT).
Es la única forma honesta de tocar `nam.h`: sin oráculo, un cambio DSP se estaría
adivinando.

## Qué hay

| Fichero | Para qué |
|---|---|
| `ref.cpp` | Renderizador: carga un `.nam` con el core oficial y procesa un fichero de floats crudos. |
| `ref.exe` | El anterior ya compilado (cl /O2, `NAM_SAMPLE` = float32). |
| `_buildref.bat` | Cómo se compila (necesita el core, ver abajo). |
| `model.cpp`, `film.h` | Copia de `NAM/wavenet/model.cpp` y `NAM/film.h` del core: la referencia que se lee para comparar el camino de A2 (condición, FiLM, head). |
| `m_*.nam` | Modelos de ejemplo del core (A2, gating, LSTM, slimmable…) para la escalera de pruebas. |
| `nested_alone.nam` | El submodelo `condition_dsp` de `wavenet_condition_dsp.nam` extraído: sirve para bisecar (¿falla el submodelo o su uso?). |
| `_t3kprobe.cpp` / `_t3kprobe.exe` | Sonda del **flujo de conexión de TONE3000** contra el servidor real: crea el código de dispositivo y sondea como la app. `_t3kprobe.exe [clave] [only]` (`only` solo crea e imprime el `DEVICE_CODE`). Se compila con `_buildprobe.bat`. |

## Cómo se verifica el motor NAM

```
native\_dsp_test.exe nam2 <modelo.nam>     # llama a ref.exe y compara muestra a muestra
native\_dsp_test.exe selftest              # incluye el caso A2 si ref.exe esta
```

El harness escribe la entrada, invoca `_nmaref\ref.exe`, lee su salida y compara
**todos los canales**. Criterio: menos de 1e-5 relativo.

## Recompilar el oráculo (necesita red)

```bash
git clone --depth 1 --branch v0.6.0 --recursive \
    https://github.com/sdatkinson/NeuralAmpModelerCore.git core
# y despues _buildref.bat (deja core/ fuera del repo: son ~277 MB con Eigen)
```

`core/` no se guarda: se vuelve a clonar cuando haga falta. Los `.f32` de
intercambio tampoco (los regenera cada ejecución).
