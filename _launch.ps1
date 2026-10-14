# Lanzador de GuitarAmp: compila si el fuente cambio y abre la app.
#  Usa Start-Process (CreateProcess), NO "start"/ShellExecute: con ShellExecute
#  Windows interpone el aviso de "editor desconocido" y hay que pulsar Ejecutar
#  cada vez que se abre.
$ErrorActionPreference = 'Continue'
$root = 'C:\Users\Usuario\Desktop\Nueva carpeta (7)'
$exe  = Join-Path $root 'native\GuitarAmp.exe'
$src  = Join-Path $root 'native\GuitarAmpNative.cpp'
$log  = Join-Path $env:TEMP 'guitaramp_build.log'

Get-Process GuitarAmp -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 250

$need = $true
if ((Test-Path $exe) -and (Test-Path $src)) {
    if ((Get-Item $src).LastWriteTime -le (Get-Item $exe).LastWriteTime) { $need = $false }
}
if ($need) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root '_build.ps1') *> $log
    if (-not (Select-String -Path $log -Pattern 'BUILD_OK' -Quiet)) {
        Add-Type -AssemblyName PresentationFramework
        [System.Windows.MessageBox]::Show((Get-Content -Raw $log), 'GuitarAmp - error de compilacion') | Out-Null
        exit 1
    }
}

# CreateProcess puro (UseShellExecute = $false): asi Windows NO interpone el
# aviso de "editor desconocido" que obliga a pulsar Ejecutar cada vez.
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.WorkingDirectory = (Split-Path $exe)
$psi.UseShellExecute = $false
[System.Diagnostics.Process]::Start($psi) | Out-Null
