$ErrorActionPreference = 'Stop'
# Raiz del repo = carpeta de este script (no una ruta de esta maquina: el build
# tiene que funcionar en cualquier equipo donde se clone el repo).
$root = Join-Path $PSScriptRoot 'native'

# vcvars64: Build Tools puede estar en Arquitectura de 32 bits o de 64.
$vcCandidates = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
)
$vc = $vcCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vc) { Write-Output 'BUILD_FAILED no se encuentra vcvars64.bat (Visual Studio 2022 Build Tools)'; exit 1 }

Set-Location $root
Get-Process GuitarAmp -ErrorAction SilentlyContinue | Stop-Process -Force

# --- Recursos del .exe: icono y ficha de version --------------------------
# Sin un .res enlazado el Explorador y la barra de tareas muestran el icono
# generico de Windows hasta que la app arranca (y en la barra de titulo, siempre).
$rc = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\rc.exe' -ErrorAction SilentlyContinue |
      Sort-Object { [Version]$_.Directory.Parent.Name } -Descending |
      Select-Object -First 1 -ExpandProperty FullName
if (-not $rc) { Write-Output 'BUILD_FAILED no se encuentra rc.exe (Windows SDK)'; exit 1 }
# rc.exe no trae las rutas del SDK: windows.h sale de <Kits>\10\Include\<ver>\um y shared.
$sdkInc = Join-Path 'C:\Program Files (x86)\Windows Kits\10\Include' (Split-Path (Split-Path (Split-Path $rc -Parent) -Parent) -Leaf)
& $rc /nologo /I "$sdkInc\um" /I "$sdkInc\shared" /fo GuitarAmp.res GuitarAmp.rc
if ($LASTEXITCODE -ne 0) { Write-Output ('BUILD_FAILED rc ' + $LASTEXITCODE); exit 1 }

# /utf-8: la fuente esta en UTF-8. Sin esta bandera MSVC la lee con la pagina de
# codigos local y cualquier literal ancho con tilde sale corrupto.
$cmd = 'call "' + $vc + '" >nul 2>&1 && cl /nologo /utf-8 /O2 /EHsc /std:c++17 GuitarAmpNative.cpp /Fe:GuitarAmp.exe /link ole32.lib oleaut32.lib uuid.lib user32.lib gdi32.lib propsys.lib gdiplus.lib comdlg32.lib dwmapi.lib winhttp.lib bcrypt.lib crypt32.lib shell32.lib avrt.lib GuitarAmp.res'
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { Write-Output ('BUILD_FAILED ' + $LASTEXITCODE); exit 1 }

# --- Firma del ejecutable -------------------------------------------------
# Sin firma, Windows abre un aviso de "editor desconocido" antes de ejecutar
# el .exe (y hay que pulsar Ejecutar cada vez). Se firma con el certificado
# de desarrollo local "GuitarAmp Dev", ya confiado en el almacen del usuario.
# El SDK puede estar en cualquier version instalada: se toma la mas nueva.
$signtool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe' -ErrorAction SilentlyContinue |
            Sort-Object { [Version]($_.Directory.Parent.Name -replace '[^\d.]','') } -Descending |
            Select-Object -First 1 -ExpandProperty FullName
$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
        Where-Object { $_.Subject -eq 'CN=GuitarAmp Dev' } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
if ($signtool -and $cert) {
    & $signtool sign /fd SHA256 /sha1 $cert.Thumbprint "$root\GuitarAmp.exe" | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Output 'SIGN_FAILED' } else { Write-Output 'SIGNED' }
} else {
    Write-Output 'SIGN_SKIPPED'
}
Write-Output 'BUILD_OK'
