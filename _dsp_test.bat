@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd native
cl /nologo /O2 /EHsc /std:c++17 /DGA_HARNESS GuitarAmpNative.cpp /Fe:_dsp_test.exe /link ole32.lib oleaut32.lib uuid.lib user32.lib gdi32.lib msimg32.lib propsys.lib gdiplus.lib comdlg32.lib dwmapi.lib >nul 2>&1
if errorlevel 1 (echo BUILD_FAIL) else (_dsp_test.exe extra)
