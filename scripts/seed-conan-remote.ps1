# Seed the team Conan remote with pre-built packages.
# Uses SSH tunnel to bypass Cloudflare timeout on large uploads.
#
# Usage:
#   $env:CONAN_REMOTE_PASSWORD = "password"
#   .\scripts\seed-conan-remote.ps1                    # Release (default)
#   .\scripts\seed-conan-remote.ps1 -BuildType Debug   # Debug
param(
    [string]$BuildType = "Release",
    [string]$RemoteName = "team-conan-direct",
    [string]$SshHost = "gavin@100.81.210.49",
    [int]$LocalPort = 9300
)

$ErrorActionPreference = "Stop"

if (-not $env:CONAN_REMOTE_PASSWORD) {
    Write-Host "ERROR: Set `$env:CONAN_REMOTE_PASSWORD first." -ForegroundColor Red
    exit 1
}

Write-Host "=== Seeding Conan Remote ($BuildType) ===" -ForegroundColor Cyan

# Start SSH tunnel for direct upload (bypasses Cloudflare timeout)
Write-Host "Starting SSH tunnel ($SshHost -> localhost:$LocalPort)..." -ForegroundColor Yellow
$tunnel = Start-Process -NoNewWindow -PassThru ssh "-N -L ${LocalPort}:localhost:${LocalPort} $SshHost"
Start-Sleep -Seconds 2

if ($tunnel.HasExited) {
    Write-Host "ERROR: SSH tunnel failed to start." -ForegroundColor Red
    exit 1
}

try {
    # Add temporary direct remote pointing at SSH tunnel
    conan remote add $RemoteName "http://localhost:$LocalPort" --index 0 --force
    conan remote login $RemoteName ci -p $env:CONAN_REMOTE_PASSWORD

    # Show profile for verification
    Write-Host "`nLocal Conan profile:" -ForegroundColor Yellow
    conan profile show

    # Build all deps from source if not already cached
    Write-Host "`nInstalling dependencies (build_type=$BuildType, --build=missing)..." -ForegroundColor Yellow
    conan install . -of build --build=missing -s build_type=$BuildType

    # Upload directly through tunnel
    Write-Host "`nUploading all packages to $RemoteName..." -ForegroundColor Yellow
    conan upload "*" -r $RemoteName -c

    Write-Host "`n=== Seed Complete ($BuildType) ===" -ForegroundColor Green
} finally {
    # Clean up temporary remote and SSH tunnel
    conan remote remove $RemoteName 2>$null
    if (-not $tunnel.HasExited) {
        Stop-Process -Id $tunnel.Id -ErrorAction SilentlyContinue
        Write-Host "SSH tunnel closed." -ForegroundColor Gray
    }
}
