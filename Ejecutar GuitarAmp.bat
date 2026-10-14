@echo off
rem  GuitarAmp: compila si hace falta y abre. Sin consola ni avisos.
rem  Doble clic en "GuitarAmp.vbs" (o en este archivo).
powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0_launch.ps1"
exit /b 0
