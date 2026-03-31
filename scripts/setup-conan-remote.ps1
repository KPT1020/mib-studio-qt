# One-time setup: add team Conan remote (Artifactory) as primary remote.
# Usage: .\scripts\setup-conan-remote.ps1 [-Url <url>] [-RemoteName <name>]

param(
    [string]$Url = "https://conan.yofo.bio/artifactory/api/conan/conan",
    [string]$RemoteName = "team-conan"
)

$ErrorActionPreference = "Stop"

$existing = conan remote list 2>&1 | Select-String $RemoteName
if ($existing) {
    Write-Host "Remote '$RemoteName' already configured" -ForegroundColor Green
} else {
    conan remote add $RemoteName $Url --index 0
    Write-Host "Added remote '$RemoteName' at index 0" -ForegroundColor Green
    Write-Host "ConanCenter remains as fallback at lower priority"
}

Write-Host "`nCurrent remotes:"
conan remote list
