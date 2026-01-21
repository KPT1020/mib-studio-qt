# PowerShell script to publish MIB Studio Qt updates to RustFS (S3-compatible)
# Usage: .\publish-update.ps1 -Installer "build\dist\MIB_Studio_Qt_Update_v0.2.0.exe" -Profile rustfs
#        .\publish-update.ps1 -Installer "build\dist\MIB_Studio_Qt_Setup_v0.2.0.exe" -Profile rustfs
# Version is auto-detected from installer filename if not provided

param(
    [Parameter(Mandatory=$false)]
    [string]$Version,
    
    [Parameter(Mandatory=$true)]
    [string]$Installer,
    
    [string]$Endpoint = "https://s3.yofo.bio",
    [string]$Bucket = "mib-studio-qt-updates",
    [string]$Channel = "stable",
    [string]$Profile = "",
    [string]$ReleaseNotesUrl = ""
)

$ErrorActionPreference = "Stop"

Write-Host "=== Publishing MIB Studio Qt Update ===" -ForegroundColor Cyan

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

# Build URLs
$endpointNoSlash = $Endpoint.TrimEnd('/')
$installerUrl = "$endpointNoSlash/$Bucket/$installerKey"

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

# Build AWS CLI base args
$awsArgs = @("--endpoint-url", $Endpoint)
if ($Profile) {
    $awsArgs += @("--profile", $Profile)
}

# Upload installer (always use public-read ACL)
Write-Host "`n3. Uploading installer..." -ForegroundColor Yellow
$installerArgs = $awsArgs + @("s3", "cp", $Installer, "s3://$Bucket/$installerKey", "--content-type", "application/octet-stream", "--acl", "public-read")

Write-Host "   Command: aws $($installerArgs -join ' ')" -ForegroundColor Gray
$result = & aws $installerArgs 2>&1

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Installer upload failed!" -ForegroundColor Red
    Write-Host $result -ForegroundColor Red
    Remove-Item $manifestPath -ErrorAction SilentlyContinue
    exit 1
}

if ($result) {
    Write-Host "   $result" -ForegroundColor Gray
}
Write-Host "   Installer uploaded successfully" -ForegroundColor Green

# Upload manifest (always use public-read ACL)
Write-Host "`n4. Uploading manifest..." -ForegroundColor Yellow
$manifestArgs = $awsArgs + @("s3", "cp", $manifestPath, "s3://$Bucket/$manifestKey", "--content-type", "application/json", "--acl", "public-read")

Write-Host "   Command: aws $($manifestArgs -join ' ')" -ForegroundColor Gray
$result = & aws $manifestArgs 2>&1

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Manifest upload failed!" -ForegroundColor Red
    Write-Host $result -ForegroundColor Red
    Remove-Item $manifestPath -ErrorAction SilentlyContinue
    exit 1
}

if ($result) {
    Write-Host "   $result" -ForegroundColor Gray
}
Write-Host "   Manifest uploaded successfully" -ForegroundColor Green

# Cleanup
Remove-Item $manifestPath -ErrorAction SilentlyContinue

Write-Host "`n=== Publish Complete ===" -ForegroundColor Cyan
Write-Host "Manifest URL: $endpointNoSlash/$Bucket/$manifestKey" -ForegroundColor Green
Write-Host "Installer URL: $installerUrl" -ForegroundColor Green
