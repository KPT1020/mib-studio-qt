# Publish MIB Studio Tools zip to Cloudflare R2 (S3-compatible) under stable/tools/
# Usage: $env:MIB_STUDIO_R2_ENDPOINT = "https://<account-id>.r2.cloudflarestorage.com"
#        .\publish-tools.ps1 -Zip "tools\dist\MIB_Studio_Tools_v0.1.7_windows.zip" [-Profile mib-studio-r2]
# Version is auto-detected from zip filename if not provided.

param(
    [Parameter(Mandatory=$false)]
    [string]$Version,

    [Parameter(Mandatory=$false)]
    [string]$Zip,

    [string]$Endpoint = $env:MIB_STUDIO_R2_ENDPOINT,
    [string]$Bucket = "mib-studio-qt-updates",
    [string]$PublicBaseUrl = "https://updates.yofo.bio",
    [string]$Channel = "stable",
    [string]$Profile = $env:MIB_STUDIO_R2_PROFILE,
    [string]$Acl = ""
)

$ErrorActionPreference = "Stop"

function Join-PublicObjectUrl {
    param(
        [string]$BaseUrl,
        [string]$Key
    )

    return "$($BaseUrl.TrimEnd('/'))/$($Key.TrimStart('/'))"
}

function Invoke-S3Upload {
    param(
        [string]$File,
        [string]$Key,
        [string]$ContentType
    )

    $uploadArgs = @(
        $s3UploadScript,
        "--endpoint", $Endpoint,
        "--bucket", $Bucket,
        "--key", $Key,
        "--file", $File,
        "--content-type", $ContentType
    )
    if ($Profile) { $uploadArgs += @("--profile", $Profile) }
    if ($Acl) { $uploadArgs += @("--acl", $Acl) }
    if ($env:S3_UPLOAD_DEBUG) { $uploadArgs += @("--debug") }

    Write-Host "   Command: python $($uploadArgs -join ' ')" -ForegroundColor Gray
    & python $uploadArgs | Write-Host
    return $LASTEXITCODE
}

if (-not $Endpoint) {
    Write-Host "ERROR: R2 S3 API endpoint is required. Set MIB_STUDIO_R2_ENDPOINT or pass -Endpoint." -ForegroundColor Red
    Write-Host "       Example: https://<account-id>.r2.cloudflarestorage.com" -ForegroundColor Red
    exit 1
}

$s3UploadScript = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "scripts\s3_upload.py"
if (-not (Test-Path $s3UploadScript)) {
    Write-Host "ERROR: Cannot find $s3UploadScript" -ForegroundColor Red
    exit 1
}

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

$publicBaseNoSlash = $PublicBaseUrl.TrimEnd('/')
$zipUrl = Join-PublicObjectUrl -BaseUrl $PublicBaseUrl -Key $zipKey

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

Write-Host "`n3. Uploading zip..." -ForegroundColor Yellow
$uploadExit = Invoke-S3Upload -File $Zip -Key $zipKey -ContentType "application/zip"
if ($uploadExit -ne 0) {
    Write-Host "ERROR: Zip upload failed!" -ForegroundColor Red
    Remove-Item $manifestPath -ErrorAction SilentlyContinue
    exit 1
}
Write-Host "   Zip uploaded successfully" -ForegroundColor Green

Write-Host "`n4. Uploading tools-latest.json..." -ForegroundColor Yellow
$uploadExit = Invoke-S3Upload -File $manifestPath -Key $manifestKey -ContentType "application/json"
Remove-Item $manifestPath -ErrorAction SilentlyContinue
if ($uploadExit -ne 0) {
    Write-Host "ERROR: Manifest upload failed!" -ForegroundColor Red
    exit 1
}
Write-Host "   Manifest uploaded successfully" -ForegroundColor Green

Write-Host "`n=== Publish Complete ===" -ForegroundColor Cyan
Write-Host "Manifest URL: $(Join-PublicObjectUrl -BaseUrl $publicBaseNoSlash -Key $manifestKey)" -ForegroundColor Green
Write-Host "Zip URL: $zipUrl" -ForegroundColor Green
