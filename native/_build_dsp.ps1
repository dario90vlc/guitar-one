$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$vc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
Set-Location $root
# /utf-8: la fuente esta en UTF-8. Sin esta bandera MSVC la lee con la pagina de
# codigos local y cualquier literal ancho con tilde sale corrupto.
# La lista de librerias es la MISMA que la de la app (_build.ps1): el comando
# `render` del banco llama a WndProc/paintWindow, asi que ahora se enlaza la UI
# entera y hacen falta winhttp y shell32 (el panel TONE3000 las usa). Antes no
# hacian falta solo porque el enlazador descartaba la UI como codigo muerto.
$cmd = 'call "' + $vc + '" >nul 2>&1 && cl /nologo /utf-8 /O2 /EHsc /std:c++17 /DGA_HARNESS GuitarAmpNative.cpp /Fe:_dsp_test.exe /link ole32.lib oleaut32.lib uuid.lib user32.lib gdi32.lib propsys.lib gdiplus.lib comdlg32.lib dwmapi.lib winhttp.lib bcrypt.lib crypt32.lib shell32.lib'
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { Write-Output ('BUILD_DSP_FAILED ' + $LASTEXITCODE); exit 1 }
Write-Output 'BUILD_DSP_OK'
