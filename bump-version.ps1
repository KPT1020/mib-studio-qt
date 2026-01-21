# PowerShell script to bump version in CMakeLists.txt
# Usage: .\bump-version.ps1 --patch|--minor|--major [--tag]

param(
    [Parameter(Mandatory=$false)]
    [Alias("p")]
    [switch]$Patch,
    
    [Parameter(Mandatory=$false)]
    [Alias("m")]
    [switch]$Minor,
    
    [Parameter(Mandatory=$false)]
    [switch]$Major,
    
    [Parameter(Mandatory=$false)]
    [switch]$Tag
)

$ErrorActionPreference = "Stop"

# Validate exactly one bump type is specified
$bumpCount = 0
if ($Patch) { $bumpCount++ }
if ($Minor) { $bumpCount++ }
if ($Major) { $bumpCount++ }

if ($bumpCount -eq 0) {
    Write-Host "ERROR: Must specify one of --patch, --minor, or --major" -ForegroundColor Red
    Write-Host "Usage: .\bump-version.ps1 --patch|--minor|--major [--tag]" -ForegroundColor Yellow
    exit 1
}

if ($bumpCount -gt 1) {
    Write-Host "ERROR: Can only specify one bump type at a time" -ForegroundColor Red
    exit 1
}

# Find CMakeLists.txt
$cmakeFile = Join-Path $PSScriptRoot "CMakeLists.txt"
if (-not (Test-Path $cmakeFile)) {
    Write-Host "ERROR: CMakeLists.txt not found at: $cmakeFile" -ForegroundColor Red
    exit 1
}

Write-Host "=== Version Bump Tool ===" -ForegroundColor Cyan

# Read and extract current version from DEFAULT_VERSION
$content = Get-Content $cmakeFile -Raw
if ($content -match 'set\(DEFAULT_VERSION\s+"(\d+\.\d+\.\d+)"\)') {
    $currentVersion = $matches[1]
    Write-Host "Current version: $currentVersion" -ForegroundColor Green
} else {
    Write-Host "ERROR: Could not find DEFAULT_VERSION in CMakeLists.txt" -ForegroundColor Red
    Write-Host "Expected format: set(DEFAULT_VERSION \"X.Y.Z\")" -ForegroundColor Yellow
    exit 1
}

# Parse version components
$versionParts = $currentVersion -split '\.'
if ($versionParts.Length -ne 3) {
    Write-Host "ERROR: Invalid version format: $currentVersion" -ForegroundColor Red
    Write-Host "Expected semantic version format: X.Y.Z" -ForegroundColor Yellow
    exit 1
}

$versionMajor = [int]$versionParts[0]
$versionMinor = [int]$versionParts[1]
$versionPatch = [int]$versionParts[2]

# Calculate new version
if ($Major) {
    $versionMajor++
    $versionMinor = 0
    $versionPatch = 0
    $bumpType = "major"
} elseif ($Minor) {
    $versionMinor++
    $versionPatch = 0
    $bumpType = "minor"
} else {
    $versionPatch++
    $bumpType = "patch"
}

$newVersion = "$versionMajor.$versionMinor.$versionPatch"
Write-Host "New version: $newVersion ($bumpType bump)" -ForegroundColor Cyan

# Confirm before updating
Write-Host "`nUpdating CMakeLists.txt..." -ForegroundColor Yellow

# Replace DEFAULT_VERSION in CMakeLists.txt
$newContent = $content -replace "set\(DEFAULT_VERSION\s+`"$currentVersion`"\)", "set(DEFAULT_VERSION `"$newVersion`")"

# Verify the replacement worked
if ($newContent -eq $content) {
    Write-Host "ERROR: Failed to update version in CMakeLists.txt" -ForegroundColor Red
    exit 1
}

# Write updated content
Set-Content -Path $cmakeFile -Value $newContent -NoNewline
Write-Host "CMakeLists.txt updated successfully" -ForegroundColor Green

# Create git tag if requested
if ($Tag) {
    Write-Host "`nCreating git tag..." -ForegroundColor Yellow
    
    # Check if git is available
    $gitAvailable = $false
    try {
        $null = git --version 2>&1
        $gitAvailable = $true
    } catch {
        $gitAvailable = $false
    }
    
    if (-not $gitAvailable) {
        Write-Host "WARNING: Git is not available. Skipping tag creation." -ForegroundColor Yellow
    } else {
        # Check if we're in a git repository
        $gitRepo = $false
        try {
            $null = git rev-parse --git-dir 2>&1
            $gitRepo = $true
        } catch {
            $gitRepo = $false
        }
        
        if (-not $gitRepo) {
            Write-Host "WARNING: Not in a git repository. Skipping tag creation." -ForegroundColor Yellow
        } else {
            $tagName = "v$newVersion"
            
            # Check if tag already exists
            $tagExists = $false
            try {
                $null = git rev-parse "refs/tags/$tagName" 2>&1
                $tagExists = $true
            } catch {
                $tagExists = $false
            }
            
            if ($tagExists) {
                Write-Host "WARNING: Tag $tagName already exists. Skipping tag creation." -ForegroundColor Yellow
            } else {
                # Create annotated tag
                git tag -a "$tagName" -m "Version $newVersion"
                if ($LASTEXITCODE -eq 0) {
                    Write-Host "Git tag created: $tagName" -ForegroundColor Green
                    Write-Host "Note: Push tags with: git push origin $tagName" -ForegroundColor Cyan
                } else {
                    Write-Host "ERROR: Failed to create git tag" -ForegroundColor Red
                    exit 1
                }
            }
        }
    }
}

Write-Host "`n=== Version Bump Complete ===" -ForegroundColor Cyan
Write-Host "Version updated: $currentVersion to $newVersion" -ForegroundColor Green
