# ============================================================================
#  Empaqueta la release de GUITAR ONE para GitHub.
#
#  Deja en dist\ los dos ficheros que se suben a la Release:
#     GuitarAmp-<ver>-portable.exe   un solo .exe, sin instalador ni carpeta
#     GuitarAmp-Setup-<ver>.exe      instalador normal (Inno Setup)
#
#  Los dos llevan dentro los 26 PNG del arte (RCDATA, ver _build.ps1), asi que
#  ninguno necesita la carpeta amps_frontales al lado.
#
#  El .exe solo enlaza DLLs del sistema (ole32, user32, gdi32, gdiplus,
#  comdlg32, dwmapi, winhttp, crypt32, shell32, avrt, kernel32): NO necesita
#  VC++ Redistributable. Comprobarlo con:
#     dumpbin /nologo /dependents GuitarAmp.exe
# ============================================================================
param([string]$Version = '1.0.1', [switch]$NoInstaller)
$ErrorActionPreference = 'Stop'
$root   = $PSScriptRoot
$native = Join-Path $root 'native'
$dist   = Join-Path $root 'dist'
$exe    = Join-Path $native 'GuitarAmp.exe'

if (-not (Test-Path $exe)) { throw "falta $exe : compila antes (.\_build.ps1)" }
$exeTime = (Get-Item $exe).LastWriteTime
$srcTime = (Get-Item (Join-Path $native 'GuitarAmpNative.cpp')).LastWriteTime
if ($srcTime -gt $exeTime) {
    Write-Output 'AVISO: el .exe es mas viejo que el fuente (compila antes de publicar)'
}

New-Item -ItemType Directory -Force -Path $dist | Out-Null

# --- Portable: el .exe tal cual, renombrado con la version -------------------
$portable = Join-Path $dist "GuitarAmp-$Version-portable.exe"
Copy-Item $exe $portable -Force

$bytes = (Get-Item $portable).Length

# --- Instalador normal ------------------------------------------------------
$setup = $null
if (-not $NoInstaller) {
    $iscc = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
        'C:\Program Files\Inno Setup 6\ISCC.exe'
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { throw 'no se encuentra ISCC.exe (Inno Setup 6)' }

    # La version del .iss manda: si no coincide con -Version, el nombre del
    # fichero y la ficha de version mentirian en sitios distintos.
    $issText = Get-Content (Join-Path $root '_installer.iss') -Raw
    if ($issText -notmatch ('#define AppVersion "' + [regex]::Escape($Version) + '"')) {
        throw "el _installer.iss no declara AppVersion ""$Version"""
    }

    Set-Location $root
    & $iscc '/Qp' '_installer.iss'
    if ($LASTEXITCODE -ne 0) { throw "ISCC fallo con codigo $LASTEXITCODE" }
    $setup = Join-Path $dist "GuitarAmp-Setup-$Version.exe"
    if (-not (Test-Path $setup)) { throw "el instalador no aparece en $setup" }
}

"PORTABLE   : $portable  ($([math]::Round($bytes/1KB,0)) KB)"
if ($setup) { "INSTALADOR : $setup  ($([math]::Round((Get-Item $setup).Length/1KB,0)) KB)" }
"EXE FUENTE : $exeTime"
