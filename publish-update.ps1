# PowerShell script to publish MIB Studio Qt updates to Cloudflare R2 (S3-compatible)
# Usage: $env:MIB_STUDIO_R2_ENDPOINT = "https://<account-id>.r2.cloudflarestorage.com"
#        .\publish-update.ps1 -Installer "build\dist\MIB_Studio_Qt_Update_v0.2.0.exe" -Profile mib-studio-r2
#        .\publish-update.ps1 -Installer "build\dist\MIB_Studio_Qt_Setup_v0.2.0.exe" -Profile mib-studio-r2
# Version is auto-detected from installer filename if not provided

param(
    [Parameter(Mandatory=$false)]
    [string]$Version,
    
    [Parameter(Mandatory=$true)]
    [string]$Installer,
    
    [string]$Endpoint = $env:MIB_STUDIO_R2_ENDPOINT,
    [string]$Bucket = "mib-studio-qt-updates",
    [string]$PublicBaseUrl = "https://updates.yofo.bio",
    [string]$Channel = "stable",
    [string]$Profile = $env:MIB_STUDIO_R2_PROFILE,
    [string]$Acl = "",
    [string]$ReleaseNotesUrl = ""
)

$ErrorActionPreference = "Stop"

Write-Host "=== Publishing MIB Studio Qt Update ===" -ForegroundColor Cyan

if (-not $Endpoint) {
    Write-Host "ERROR: R2 S3 API endpoint is required. Set MIB_STUDIO_R2_ENDPOINT or pass -Endpoint." -ForegroundColor Red
    Write-Host "       Example: https://<account-id>.r2.cloudflarestorage.com" -ForegroundColor Red
    exit 1
}

function Join-PublicObjectUrl {
    param(
        [string]$BaseUrl,
        [string]$Key
    )

    return "$($BaseUrl.TrimEnd('/'))/$($Key.TrimStart('/'))"
}

# Locate boto3 uploader helper
$s3UploadScript = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "scripts\s3_upload.py"
if (-not (Test-Path $s3UploadScript)) {
    Write-Host "ERROR: Cannot find $s3UploadScript" -ForegroundColor Red
    exit 1
}

# Validate installer exists
if (-not (Test-Path $Installer)) {
    Write-Host "ERROR: Installer file does not exist: $Installer" -ForegroundColor Red
    exit 1
}

# Auto-detect version from filename if not provided
if (-not $Version) {
    $filename = Split-Path -Leaf $Installer
    if ($filename -match 'MIB_Studio_Qt_(?:Setup|Update)_v(\d+\.\d+\.\d+)\.exe$') {
        $Version = $matches[1]
        Write-Host "Extracted version from filename: $Version" -ForegroundColor Cyan
    } else {
        Write-Host "ERROR: Cannot extract version from filename. Expected pattern: MIB_Studio_Qt_Setup_v<version>.exe or MIB_Studio_Qt_Update_v<version>.exe" -ForegroundColor Red
        exit 1
    }
}

# Auto-detect package type from filename
$filename = Split-Path -Leaf $Installer
$isUpdatePackage = $filename -match 'MIB_Studio_Qt_Update_v'
if ($isUpdatePackage) {
    Write-Host "Detected update package (app files only)" -ForegroundColor Cyan
} else {
    Write-Host "Detected full installer (includes eGrabber/VC++ redistributable)" -ForegroundColor Cyan
}

$installerInfo = Get-Item $Installer
$sizeBytes = $installerInfo.Length

if ($sizeBytes -le 0) {
    Write-Host "ERROR: Installer file size is invalid: $sizeBytes" -ForegroundColor Red
    exit 1
}

# Compute SHA-256
Write-Host "`n1. Computing SHA-256 hash..." -ForegroundColor Yellow
$hash = (Get-FileHash -Algorithm SHA256 $Installer).Hash.ToLower()
Write-Host "   Hash: $hash" -ForegroundColor Green
Write-Host "   Size: $sizeBytes bytes" -ForegroundColor Green

# Build S3 keys (use actual filename to preserve Update vs Setup distinction)
$installerFileName = Split-Path -Leaf $Installer
$installerKey = "$Channel/$installerFileName"
$manifestKey = "$Channel/latest.json"

# Build public URLs. R2 custom domains expose objects by key, without the
# bucket name in the URL.
$publicBaseNoSlash = $PublicBaseUrl.TrimEnd('/')
$installerUrl = Join-PublicObjectUrl -BaseUrl $PublicBaseUrl -Key $installerKey

# Create manifest JSON
Write-Host "`n2. Generating manifest..." -ForegroundColor Yellow
$manifest = @{
    version = $Version
    installer_url = $installerUrl
    installer_sha256 = $hash
    installer_size_bytes = $sizeBytes
    published_at = (Get-Date).ToUniversalTime().ToString("o")
}

if ($ReleaseNotesUrl) {
    $manifest.release_notes_url = $ReleaseNotesUrl
}

$manifestJson = $manifest | ConvertTo-Json -Depth 10
$manifestPath = Join-Path $env:TEMP "mib_studio_qt_latest_$(New-Guid).json"
$manifestJson | Out-File -FilePath $manifestPath -Encoding UTF8 -NoNewline

Write-Host "   Manifest created: $manifestPath" -ForegroundColor Green

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
    # Pipe stdout through Write-Host so it goes to the console, not the
    # success stream. Otherwise callers that assign `$var = Invoke-S3Upload`
    # capture ["uploaded: ...", 0] and the `-ne 0` array-filter returns the
    # string element, making every successful upload look like a failure.
    & python $uploadArgs | Write-Host
    return $LASTEXITCODE
}

# Upload installer (boto3 handles multipart with correct Content-Length headers)
Write-Host "`n3. Uploading installer..." -ForegroundColor Yellow
$uploadExit = Invoke-S3Upload -File $Installer -Key $installerKey -ContentType "application/octet-stream"
if ($uploadExit -ne 0) {
    Write-Host "ERROR: Installer upload failed!" -ForegroundColor Red
    Remove-Item $manifestPath -ErrorAction SilentlyContinue
    exit 1
}
Write-Host "   Installer uploaded successfully" -ForegroundColor Green

# Upload manifest
Write-Host "`n4. Uploading manifest..." -ForegroundColor Yellow
$uploadExit = Invoke-S3Upload -File $manifestPath -Key $manifestKey -ContentType "application/json"
Remove-Item $manifestPath -ErrorAction SilentlyContinue
if ($uploadExit -ne 0) {
    Write-Host "ERROR: Manifest upload failed!" -ForegroundColor Red
    exit 1
}
Write-Host "   Manifest uploaded successfully" -ForegroundColor Green

Write-Host "`n=== Publish Complete ===" -ForegroundColor Cyan
Write-Host "Manifest URL: $(Join-PublicObjectUrl -BaseUrl $publicBaseNoSlash -Key $manifestKey)" -ForegroundColor Green
Write-Host "Installer URL: $installerUrl" -ForegroundColor Green
