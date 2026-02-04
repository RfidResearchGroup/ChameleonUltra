<#
.SYNOPSIS
    Compila el firmware de ChameleonUltra (Application) y genera el paquete DFU.
    
.DESCRIPTION
    Este script configura el entorno (PATH) para usar el compilador ARM descargado localmente
    y las utilidades de Git, compila el código fuente en firmware/application usando 'make',
    y finalmente empaqueta el resultado en un archivo .zip usando 'nrfutil'.

.NOTES
    Requisitos:
    - Carpeta 'tools' con 'arm-toolchain' y 'nrfutil.exe' en la raíz del repo.
    - Git instalado (para utilidades Unix como mkdir, rm).
    - 'make' instalado (vía Scoop o Choco).
#>

$ErrorActionPreference = "Stop"

# --- Configuración de Rutas ---
$ScriptDir = $PSScriptRoot
$ToolsDir = Join-Path $ScriptDir "tools"
$FirmwareDir = Join-Path $ScriptDir "firmware"
$AppDir = Join-Path $FirmwareDir "application"
$ObjDir = Join-Path $FirmwareDir "objects"

# Rutas específicas de herramientas
# Ajusta la versión del toolchain si cambia la carpeta descomprimida
$ArmBin = Join-Path $ToolsDir "arm-toolchain\arm-gnu-toolchain-12.2.rel1-mingw-w64-i686-arm-none-eabi\bin"
$NrfUtil = Join-Path $ToolsDir "nrfutil.exe"
$GitBin = "C:\Program Files\Git\usr\bin" # Necesario para comandos unix dentro del Makefile
$DfuKey = Join-Path $ScriptDir "resource\dfu_key\chameleon.pem"

# --- Verificaciones ---
Write-Host "Verificando herramientas..." -ForegroundColor Cyan

if (-not (Test-Path "$ArmBin\arm-none-eabi-gcc.exe")) {
    Write-Error "No se encontró el compilador ARM en: $ArmBin"
    exit 1
}

if (-not (Test-Path $NrfUtil)) {
    Write-Error "No se encontró nrfutil en: $NrfUtil"
    exit 1
}

# --- Configurar Entorno ---
Write-Host "Configurando entorno de compilación..."
# Añadimos ARM y Git/usr/bin al inicio del PATH
$env:PATH = "$ArmBin;$GitBin;$env:PATH"

# --- Compilar Aplicación ---
Write-Host "Compilando Aplicación (Make)..." -ForegroundColor Cyan
Push-Location $AppDir

try {
    # Ejecutamos make. '-j' usa todos los núcleos para ir rápido.
    make -j
    if ($LASTEXITCODE -ne 0) { throw "Error en la compilación." }
}
finally {
    Pop-Location
}

# --- Generar Paquete DFU ---
Write-Host "Generando paquete DFU (ZIP)..." -ForegroundColor Cyan

# Asegurar que existe directorio objects
if (-not (Test-Path $ObjDir)) { New-Item -ItemType Directory -Path $ObjDir | Out-Null }

$AppHex = Join-Path $ObjDir "application.hex"
$OutputZip = Join-Path $ObjDir "ultra-dfu-app.zip"

if (-not (Test-Path $AppHex)) {
    Write-Error "No se encontró el archivo compilado: $AppHex"
    exit 1
}

# Comando nrfutil para generar el paquete de actualización
& $NrfUtil pkg generate --hw-version 0 `
    --key-file $DfuKey `
    --application $AppHex `
    --application-version 1 `
    --sd-req 0x0100 `
    $OutputZip

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n---------------------------------------------------" -ForegroundColor Green
    Write-Host "¡ÉXITO! Firmware compilado y empaquetado." -ForegroundColor Green
    Write-Host "Archivo listo para flashear: $OutputZip" -ForegroundColor Yellow
    Write-Host "---------------------------------------------------"
} else {
    Write-Error "Falló la generación del paquete DFU."
}
