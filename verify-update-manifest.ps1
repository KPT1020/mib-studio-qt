# Verify that an update manifest and its referenced installer are publicly reachable.

param(
    [string]$ManifestUrl = "https://updates.yofo.bio/stable/latest.json"
)

$ErrorActionPreference = "Stop"

function Get-HeaderValue {
    param(
        [object]$Headers,
        [string]$Name
    )

    $value = $Headers[$Name]
    if ($value -is [array]) {
        return $value[0]
    }
    return $value
}

function Assert-HttpSuccess {
    param(
        [string]$Label,
        [object]$Response
    )

    $statusCode = [int]$Response.StatusCode
    if ($statusCode -lt 200 -or $statusCode -ge 300) {
        throw "$Label returned HTTP $statusCode"
    }
}

Write-Host "=== Verifying MIB Studio update manifest ===" -ForegroundColor Cyan
Write-Host "Manifest URL: $ManifestUrl" -ForegroundColor Yellow

$manifestResponse = Invoke-WebRequest -Uri $ManifestUrl -Method Get -UseBasicParsing
Assert-HttpSuccess -Label "Manifest" -Response $manifestResponse

$manifest = $manifestResponse.Content | ConvertFrom-Json
$requiredFields = @("version", "installer_url", "installer_sha256", "installer_size_bytes")
foreach ($field in $requiredFields) {
    if (-not $manifest.PSObject.Properties.Name.Contains($field)) {
        throw "Manifest is missing required field: $field"
    }
}

if (-not ([System.Uri]::IsWellFormedUriString($manifest.installer_url, [System.UriKind]::Absolute))) {
    throw "Manifest installer_url is not an absolute URL: $($manifest.installer_url)"
}

if ($manifest.installer_sha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw "Manifest installer_sha256 is not a 64-character SHA-256 hex string"
}

$expectedSize = [int64]$manifest.installer_size_bytes
if ($expectedSize -le 0) {
    throw "Manifest installer_size_bytes must be greater than zero"
}

Write-Host "Version: $($manifest.version)" -ForegroundColor Green
Write-Host "Installer URL: $($manifest.installer_url)" -ForegroundColor Yellow

$usedRangeProbe = $false
try {
    $installerResponse = Invoke-WebRequest -Uri $manifest.installer_url -Method Head -UseBasicParsing
} catch {
    Write-Host "HEAD failed, retrying installer probe with Range: bytes=0-0" -ForegroundColor Yellow
    $usedRangeProbe = $true
    $installerResponse = Invoke-WebRequest -Uri $manifest.installer_url -Method Get -Headers @{ Range = "bytes=0-0" } -UseBasicParsing
}

Assert-HttpSuccess -Label "Installer" -Response $installerResponse

$contentLength = Get-HeaderValue -Headers $installerResponse.Headers -Name "Content-Length"
if ($usedRangeProbe) {
    $contentRange = Get-HeaderValue -Headers $installerResponse.Headers -Name "Content-Range"
    if ($contentRange -match '/(\d+)$') {
        $actualSize = [int64]$matches[1]
        if ($actualSize -ne $expectedSize) {
            throw "Installer Content-Range size $actualSize does not match manifest size $expectedSize"
        }
    } else {
        Write-Host "Installer Content-Range header not present; size check skipped" -ForegroundColor Yellow
    }
} elseif ($contentLength) {
    $actualSize = [int64]$contentLength
    if ($actualSize -ne $expectedSize) {
        throw "Installer Content-Length $actualSize does not match manifest size $expectedSize"
    }
}

$contentType = Get-HeaderValue -Headers $installerResponse.Headers -Name "Content-Type"
Write-Host "Installer Content-Type: $contentType" -ForegroundColor Green
if ($contentLength) {
    Write-Host "Installer Content-Length: $contentLength" -ForegroundColor Green
} else {
    Write-Host "Installer Content-Length header not present; size check skipped" -ForegroundColor Yellow
}

Write-Host "Manifest and installer are publicly reachable." -ForegroundColor Green
