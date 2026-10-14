# Script de build para GuitarAmp
# Ejecutar desde la raíz del proyecto

param(
    [string]$BuildType = "Release",
    [string]$Generator = "Ninja"
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  GuitarAmp - Script de Build" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Verificar JUCE
if (-not (Test-Path "JUCE")) {
    Write-Host "ERROR: No se encuentra la carpeta JUCE" -ForegroundColor Red
    Write-Host ""
    Write-Host "Para obtener JUCE, ejecuta:" -ForegroundColor Yellow
    Write-Host "  git clone --depth 1 --branch 7.0.12 https://github.com/juce-framework/JUCE.git JUCE" -ForegroundColor Yellow
    Write-Host ""
    exit 1
}

Write-Host "JUCE encontrado: OK" -ForegroundColor Green

# Crear directorio de build
$buildDir = "build"
if (Test-Path $buildDir) {
    Write-Host "Limpiando build anterior..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}

New-Item -ItemType Directory -Path $buildDir | Out-Null
Write-Host "Directorio build creado: OK" -ForegroundColor Green

# Configurar
Write-Host ""
Write-Host "Configurando con CMake..." -ForegroundColor Cyan
Set-Location $buildDir

$cmakeArgs = ".. -DCMAKE_BUILD_TYPE=$BuildType"
if ($Generator -ne "") {
    $cmakeArgs = "-G `"$Generator`" $cmakeArgs"
}

Invoke-Expression "cmake $cmakeArgs"
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Falló la configuración de CMake" -ForegroundColor Red
    Set-Location ..
    exit 1
}

Write-Host "Configuración: OK" -ForegroundColor Green

# Compilar
Write-Host ""
Write-Host "Compilando..." -ForegroundColor Cyan
cmake --build . --config $BuildType

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Falló la compilación" -ForegroundColor Red
    Set-Location ..
    exit 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  Compilación exitosa!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "El plugin se encuentra en:" -ForegroundColor Cyan
Write-Host "  build/GuitarAmp_artefacts/$BuildType/" -ForegroundColor Yellow
Write-Host ""

# Buscar el plugin
$plugin = Get-ChildItem -Path "GuitarAmp_artefacts" -Recurse -Filter "*.vst3" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($plugin) {
    Write-Host "Plugin: $($plugin.FullName)" -ForegroundColor Green
}

Set-Location ..
