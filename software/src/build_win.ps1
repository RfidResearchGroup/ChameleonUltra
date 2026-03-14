$ErrorActionPreference = "Continue"
$srcDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $srcDir "build"

Write-Host "=== Cleaning build directory ==="
if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir }
New-Item -ItemType Directory -Path $buildDir | Out-Null

Set-Location $buildDir

Write-Host "=== Configuring CMake ==="
& cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release 2>&1 | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) {
    Write-Host "CMAKE CONFIGURE FAILED with exit code $LASTEXITCODE"
    exit 1
}

Write-Host ""
Write-Host "=== Building ==="
& cmake --build . --config Release -- -j4 2>&1 | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) {
    Write-Host "BUILD FAILED with exit code $LASTEXITCODE"
    exit 1
}

Write-Host ""
Write-Host "=== Build Complete ==="
$binDir = Join-Path $srcDir ".." "script" "bin"
if (Test-Path $binDir) {
    Write-Host "Binaries in $binDir`:"
    Get-ChildItem $binDir -Filter "*.exe" | ForEach-Object { Write-Host "  $($_.Name) ($([math]::Round($_.Length/1024))KB)" }
} else {
    Write-Host "WARNING: bin directory not found at $binDir"
}
