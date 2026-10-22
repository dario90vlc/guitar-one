# ============================================================================
#  Empaqueta la release de GuitarAmp para GitHub.
#  Deja en dist\GuitarAmp-<ver>-win64\ exactamente lo que hace falta para
#  ejecutarla (el .exe y los 26 PNG que pide amp_frontales_data.h) y el ZIP.
#
#  El .exe solo enlaza DLLs del sistema (ole32, user32, gdi32, gdiplus,
#  comdlg32, dwmapi, winhttp, crypt32, shell32, avrt, kernel32): NO necesita
#  VC++ Redistributable. Comprobarlo con:
#     dumpbin /nologo /dependents GuitarAmp.exe
# ============================================================================
param([string]$Version = '1.0.0')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$native = Join-Path $root 'native'
$name = "GuitarAmp-$Version-win64"
$dist = Join-Path $root "dist\$name"
$exe  = Join-Path $native 'GuitarAmp.exe'

if (-not (Test-Path $exe)) { throw "falta $exe : compila antes (.\_build.ps1)" }
$exeTime = (Get-Item $exe).LastWriteTime
$srcTime = (Get-Item (Join-Path $native 'GuitarAmpNative.cpp')).LastWriteTime
if ($srcTime -gt $exeTime) {
    Write-Output "AVISO: el .exe es mas viejo que el fuente (compila antes de publicar)"
}

# --- Los PNG que la app carga de verdad (los saca de amp_frontales_data.h) ---
$hdr = Get-Content (Join-Path $native 'amp_frontales_data.h') -Raw
$art = [regex]::Matches($hdr, 'L"(fondos\\\\[^"]+|filmstrips\\\\[^"]+)"') |
       ForEach-Object { $_.Groups[1].Value -replace '\\\\', '\' } |
       Select-Object -Unique

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $dist 'amps_frontales\fondos')    | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $dist 'amps_frontales\filmstrips') | Out-Null

Copy-Item $exe (Join-Path $dist 'GuitarAmp.exe') -Force
$bytes = 0
foreach ($rel in $art) {
    $from = Join-Path (Join-Path $native 'amps_frontales') $rel
    if (-not (Test-Path $from)) { throw "falta el arte: $rel" }
    $to = Join-Path (Join-Path $dist 'amps_frontales') $rel
    Copy-Item $from $to -Force
    $bytes += (Get-Item $to).Length
}
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $dist 'LICENSE.txt') -Force

$leeme = @"
GUITAR ONE $Version  ·  simulador de amplificadores para Windows
=================================================================

COMO SE EJECUTA
  1. Descomprime esta carpeta DONDE QUIERAS (no hace falta instalador).
  2. Doble clic en GuitarAmp.exe.
  3. Pulsa AUDIO ON en la cabecera: la app arranca en silencio a proposito.

  La carpeta amps_frontales\ tiene que estar al lado del .exe: son los
  frontales de los amplificadores. Si falta, la app arranca igual pero con
  caras dibujadas por codigo.

REQUISITOS
  Windows 10 u 11 de 64 bits. Nada mas: no necesita VC++ Redistributable,
  ni .NET, ni instalar nada. El audio va por WASAPI (compartido o exclusivo);
  no hay ASIO.

LOS PERFILES SON DE SUS AUTORES
  El cabezal PERFIL carga capturas .nam y presets de TONE3000. Esos ficheros
  NO se incluyen aqui: se descargan de https://www.tone3000.com con su
  explorador integrado (boton T3K) o se anaden desde disco.

SI NO SUENA
  - Mira la linea de estado del pie: dice el motor, el dispositivo, la
    frecuencia y el buffer.
  - En AJUSTES, "Anillo corto / tardios" a 0 significa que el motor va bien.
  - Prueba FRECUENCIA = la de tu interfaz (casi siempre 48000) y BUFFER 512.

LICENCIA
  MIT. Ver LICENSE.txt. Los nombres de los amplificadores son marcas de sus
  fabricantes: esto es un homenaje, no un producto oficial ni afiliado.
"@
Set-Content -Path (Join-Path $dist 'LEEME.txt') -Value $leeme -Encoding UTF8

# --- ZIP (con la carpeta dentro, para que al descomprimir no se mezcle) ------
$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $dist -DestinationPath $zip -CompressionLevel Optimal

"ARTE       : $($art.Count) ficheros, $([math]::Round($bytes/1MB,1)) MB"
"EXE        : $([math]::Round((Get-Item (Join-Path $dist 'GuitarAmp.exe')).Length/1KB,0)) KB  ($exeTime)"
"CARPETA    : $dist"
"ZIP        : $zip  ($([math]::Round((Get-Item $zip).Length/1MB,1)) MB)"
