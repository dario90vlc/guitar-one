# attic — código archivado, no se compila

Prototipo antiguo del plugin JUCE (VST3 + Standalone) anterior a GUITAR ONE.

- `src/` — procesadores, DSP y GUI JUCE.
- `CMakeLists.txt` — configuración CMake del plugin.
- `build.ps1` — script de build CMake (exigía clonar `JUCE/` 7.0.12, que nunca
  se versionó en este repo).

Se movió aquí el 2026-10-27 porque confundía: el producto real es la app
nativa Win32 + GDI+ de `native/GuitarAmpNative.cpp` (ver `README.md`,
`_build.ps1`). Se conserva el historial con `git mv` por si algún día se
resucita alguna idea de DSP. No tocar desde la app.
