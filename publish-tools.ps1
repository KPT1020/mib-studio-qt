# Publish MIB Studio Tools zip to RustFS (S3-compatible) under stable/tools/
# Usage: .\publish-tools.ps1 -Zip "tools\dist\MIB_Studio_Tools_v0.1.7_windows.zip" [-Profile rustfs]
# Version is auto-detected from zip filename if not provided.

param(
    [Parameter(Mandatory=$false)]
    [string]$Version,

    [Parameter(Mandatory=$false)]
    [string]$Zip,

    [string]$Endpoint = "https://s3.yofo.bio",
    [string]$Bucket = "mib-studio-qt-updates",
    [string]$Channel = "stable",
    [string]$Profile = ""
)

$ErrorActionPreference = "Stop"

# Default zip path: tools/dist/MIB_Studio_Tools_vX.Y.Z_windows.zip (use latest if single zip)
if (-not $Zip) {
    $toolsDist = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "tools\dist"
    $zips = Get-ChildItem -Path $toolsDist -Filter "MIB_Studio_Tools_v*_windows.zip" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending
    if (-not $zips) {
        Write-Host "ERROR: No tools zip found in tools\dist. Build with tools\build_windows.ps1 then tools\package-tools.ps1" -ForegroundColor Red
        exit 1
    }
    $Zip = $zips[0].FullName
    Write-Host "Using zip: $Zip" -ForegroundColor Cyan
}

if (-not (Test-Path $Zip)) {
    Write-Host "ERROR: Zip file does not exist: $Zip" -ForegroundColor Red
    exit 1
}

if (-not $Version) {
    $filename = Split-Path -Leaf $Zip
    if ($filename -match 'MIB_Studio_Tools_v(\d+\.\d+\.\d+)_windows\.zip$') {
        $Version = $matches[1]
        Write-Host "Extracted version from filename: $Version" -ForegroundColor Cyan
    } else {
        Write-Host "ERROR: Cannot extract version from filename. Expected: MIB_Studio_Tools_vX.Y.Z_windows.zip" -ForegroundColor Red
        exit 1
    }
}

Write-Host "=== Publishing MIB Studio Tools ===" -ForegroundColor Cyan

$zipInfo = Get-Item $Zip
$sizeBytes = $zipInfo.Length
if ($sizeBytes -le 0) {
    Write-Host "ERROR: Zip file size is invalid" -ForegroundColor Red
    exit 1
}

Write-Host "1. Computing SHA-256 hash..." -ForegroundColor Yellow
$hash = (Get-FileHash -Algorithm SHA256 $Zip).Hash.ToLower()
Write-Host "   Hash: $hash" -ForegroundColor Green
Write-Host "   Size: $sizeBytes bytes" -ForegroundColor Green

$zipFileName = Split-Path -Leaf $Zip
$toolsPrefix = "${Channel}/tools"
$zipKey = "$toolsPrefix/$zipFileName"
$manifestKey = "$toolsPrefix/tools-latest.json"

$endpointNoSlash = $Endpoint.TrimEnd('/')
$zipUrl = "$endpointNoSlash/$Bucket/$zipKey"

Write-Host "`n2. Generating manifest..." -ForegroundColor Yellow
$manifest = @{
    version = $Version
    zip_url = $zipUrl
    zip_sha256 = $hash
    zip_size_bytes = $sizeBytes
    published_at = (Get-Date).ToUniversalTime().ToString("o")
}
$manifestJson = $manifest | ConvertTo-Json -Depth 10
$manifestPath = Join-Path $env:TEMP "mib_tools_latest_$(New-Guid).json"
$manifestJson | Out-File -FilePath $manifestPath -Encoding UTF8 -NoNewline

$awsArgs = @("--endpoint-url", $Endpoint)
if ($Profile) { $awsArgs += @("--profile", $Profile) }

Write-Host "`n3. Uploading zip..." -ForegroundColor Yellow
$uploadArgs = $awsArgs + @("s3", "cp", $Zip, "s3://$Bucket/$zipKey", "--content-type", "application/zip", "--acl", "public-read")
$result = & aws $uploadArgs 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Zip upload failed!" -ForegroundColor Red
    Write-Host $result -ForegroundColor Red
    Remove-Item $manifestPath -ErrorAction SilentlyContinue
    exit 1
}
Write-Host "   Zip uploaded successfully" -ForegroundColor Green

Write-Host "`n4. Uploading tools-latest.json..." -ForegroundColor Yellow
$manifestUploadArgs = $awsArgs + @("s3", "cp", $manifestPath, "s3://$Bucket/$manifestKey", "--content-type", "application/json", "--acl", "public-read")
$result = & aws $manifestUploadArgs 2>&1
Remove-Item $manifestPath -ErrorAction SilentlyContinue
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Manifest upload failed!" -ForegroundColor Red
    Write-Host $result -ForegroundColor Red
    exit 1
}
Write-Host "   Manifest uploaded successfully" -ForegroundColor Green

Write-Host "`n=== Publish Complete ===" -ForegroundColor Cyan
Write-Host "Manifest URL: $endpointNoSlash/$Bucket/$manifestKey" -ForegroundColor Green
Write-Host "Zip URL: $zipUrl" -ForegroundColor Green
