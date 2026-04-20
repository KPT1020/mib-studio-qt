# Run the Tauri binary with Conan package bin dirs on PATH (avoids 0xC0000135 STATUS_DLL_NOT_FOUND).
# Usage (from repo root):
#   .\scripts\run-tauri-with-conan-path.ps1
#   .\scripts\run-tauri-with-conan-path.ps1 -Debug
#
# Requires: `conan install . -of build --build=missing -s build_type=Release` so VirtualRunEnv
# generates build\conanrun.bat (Conan 2).

param(
    [switch]$Debug
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root "build"
$bt = if ($Debug) { "Debug" } else { "Release" }
$conanrun = Join-Path $build "conanrun.bat"

if (-not (Test-Path $conanrun)) {
    Write-Host "Missing $conanrun — run Conan install so VirtualRunEnv generates it, e.g.:" -ForegroundColor Yellow
    Write-Host "  conan install . -of build --build=missing -s build_type=$bt" -ForegroundColor Gray
    exit 1
}

$srcTauri = Join-Path $root "src-tauri"
Push-Location $srcTauri
try {
    $profile = if ($Debug) { "debug" } else { "release" }
    cmd /c "call `"$conanrun`" && cargo run --$profile"
} finally {
    Pop-Location
}
