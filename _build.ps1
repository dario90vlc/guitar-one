$ErrorActionPreference = 'Stop'
$root = 'C:\Users\Usuario\Desktop\Nueva carpeta (7)\native'
$vc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
Set-Location $root
Get-Process GuitarAmp -ErrorAction SilentlyContinue | Stop-Process -Force
$cmd = 'call "' + $vc + '" >nul 2>&1 && cl /nologo /O2 /EHsc /std:c++17 GuitarAmpNative.cpp /Fe:GuitarAmp.exe /link ole32.lib oleaut32.lib uuid.lib user32.lib gdi32.lib msimg32.lib propsys.lib gdiplus.lib comdlg32.lib dwmapi.lib winhttp.lib bcrypt.lib crypt32.lib shell32.lib'
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { Write-Output ('BUILD_FAILED ' + $LASTEXITCODE); exit 1 }

# --- Firma del ejecutable -------------------------------------------------
#  Sin firma, Windows abre un aviso de "editor desconocido" antes de ejecutar
#  el .exe (y hay que pulsar Ejecutar cada vez). Se firma con el certificado
#  de desarrollo local "GuitarAmp Dev", ya confiado en el almacen del usuario.
$signtool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
        Where-Object { $_.Subject -eq 'CN=GuitarAmp Dev' } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
if ((Test-Path $signtool) -and $cert) {
    & $signtool sign /fd SHA256 /sha1 $cert.Thumbprint "$root\GuitarAmp.exe" | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Output 'SIGN_FAILED' } else { Write-Output 'SIGNED' }
} else {
    Write-Output 'SIGN_SKIPPED'
}
Write-Output 'BUILD_OK'
