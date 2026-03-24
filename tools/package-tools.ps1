# Create versioned MIB Studio Tools zip for distribution.
# Run after building (.\build_windows.ps1). Version defaults to CMakeLists.txt DEFAULT_VERSION.
# Usage: .\package-tools.ps1 [-Version 0.1.7]
# Output: tools\dist\MIB_Studio_Tools_vX.Y.Z_windows.zip

param(
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"
$ToolsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ToolsDir

if (-not $Version) {
    $cmakePath = Join-Path (Split-Path -Parent $ToolsDir) "CMakeLists.txt"
    if (Test-Path $cmakePath) {
        $line = Get-Content $cmakePath | Select-String -Pattern 'set\s*\(\s*DEFAULT_VERSION\s+"([^"]+)"' | Select-Object -First 1
        if ($line) {
            $Version = $line.Matches[0].Groups[1].Value
            Write-Host "Using version from CMakeLists.txt: $Version" -ForegroundColor Cyan
        }
    }
    if (-not $Version) {
        Write-Host "ERROR: Version not provided and could not read from CMakeLists.txt. Use -Version X.Y.Z" -ForegroundColor Red
        exit 1
    }
}

$DistDir = "dist"
$zipName = "MIB_Studio_Tools_v${Version}_windows.zip"
$zipPath = Join-Path $DistDir $zipName

foreach ($exe in @("hdf5_export_app.exe", "mib_reanalyse_hdf5.exe")) {
    $p = Join-Path $DistDir $exe
    if (-not (Test-Path $p)) {
        Write-Host "ERROR: $exe not found in $DistDir. Run .\build_windows.ps1 first." -ForegroundColor Red
        exit 1
    }
}

$readmeContent = @"
MIB Studio Tools v$Version (Windows)
===================================

Contents:
  - hdf5_export_app.exe   GUI: export metrics and images from an .h5 file
  - mib_reanalyse_hdf5.exe  CLI: re-run processing pipeline on an .h5 and save intermediates

Quick usage:
  HDF5 Export (GUI):  Double-click hdf5_export_app.exe or run from command line.
  Reanalyse HDF5:     mib_reanalyse_hdf5.exe -i experiment.h5 -o ./reanalysis
                      mib_reanalyse_hdf5.exe --help

Documentation: See docs/howto/tools.md and docs/howto/reanalyse-hdf5.md in the MIB Studio Qt repository.
Compatibility: Tools v$Version are intended for use with MIB Studio Qt v$Version HDF5 schema.
"@

$readmePath = Join-Path $env:TEMP "MIB_Studio_Tools_README.txt"
$readmeContent | Out-File -FilePath $readmePath -Encoding UTF8

# Build zip in a temp folder so we only include the exes + README
$tempDir = Join-Path $env:TEMP "mib_tools_zip_$(New-Guid)"
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
Copy-Item (Join-Path $DistDir "hdf5_export_app.exe") $tempDir
Copy-Item (Join-Path $DistDir "mib_reanalyse_hdf5.exe") $tempDir
Copy-Item $readmePath (Join-Path $tempDir "README.txt")

if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path "$tempDir\*" -DestinationPath $zipPath -CompressionLevel Optimal
Remove-Item -Recurse -Force $tempDir
Remove-Item $readmePath -ErrorAction SilentlyContinue

$sizeMB = [math]::Round((Get-Item $zipPath).Length / 1MB, 2)
Write-Host "Created: $zipPath ($sizeMB MB)" -ForegroundColor Green
