@echo off
rem Sonda del flujo de conexion de TONE3000 (se recompila desde cualquier carpeta).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0.."
cl /nologo /O2 /EHsc /std:c++17 /I . "%~dp0_t3kprobe.cpp" /Fe:"%~dp0_t3kprobe.exe" /link winhttp.lib crypt32.lib bcrypt.lib
