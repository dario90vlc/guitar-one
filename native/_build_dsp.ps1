$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$vc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
Set-Location $root
$cmd = 'call "' + $vc + '" >nul 2>&1 && cl /nologo /O2 /EHsc /std:c++17 /DGA_HARNESS GuitarAmpNative.cpp /Fe:_dsp_test.exe /link ole32.lib oleaut32.lib uuid.lib user32.lib gdi32.lib msimg32.lib propsys.lib gdiplus.lib comdlg32.lib dwmapi.lib crypt32.lib'
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { Write-Output ('BUILD_DSP_FAILED ' + $LASTEXITCODE); exit 1 }
Write-Output 'BUILD_DSP_OK'
