# Unified release script for MIB Studio Qt
# Orchestrates: version bump -> build -> installers -> tag -> push (triggers CI/CD)
#
# Usage:
#   .\release.ps1 --patch                    # Bump patch, build, create tag
#   .\release.ps1 --minor                    # Bump minor, build, create tag
#   .\release.ps1 --major                    # Bump major, build, create tag
#   .\release.ps1 --patch --push             # Bump, build, tag, and push (triggers release pipeline)
#   .\release.ps1 --patch --skip-build       # Bump and tag only (let CI handle the build)
#   .\release.ps1 --patch --push --skip-build # Bump, tag, push (fully CI-driven release)

param(
    [switch]$Patch,
    [switch]$Minor,
    [switch]$Major,
    [switch]$Push,
    [switch]$SkipBuild,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

# --- Validate bump type ---
$bumpCount = 0
if ($Patch) { $bumpCount++ }
if ($Minor) { $bumpCount++ }
if ($Major) { $bumpCount++ }

if ($bumpCount -ne 1) {
    Write-Host "ERROR: Specify exactly one of --patch, --minor, or --major" -ForegroundColor Red
    Write-Host ""
    Write-Host "Usage: .\release.ps1 --patch|--minor|--major [--push] [--skip-build] [--dry-run]" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Options:" -ForegroundColor Yellow
    Write-Host "  --patch       Bump patch version (bug fixes)"
    Write-Host "  --minor       Bump minor version (new features)"
    Write-Host "  --major       Bump major version (breaking changes)"
    Write-Host "  --push        Push tag to remote (triggers CI/CD release)"
    Write-Host "  --skip-build  Skip local build (let CI handle it)"
    Write-Host "  --dry-run     Show what would happen without making changes"
    exit 1
}

# --- Check git status ---
Write-Host "=== MIB Studio Qt Release ===" -ForegroundColor Cyan

$gitStatus = git status --porcelain
if ($gitStatus) {
    Write-Host "WARNING: Working tree has uncommitted changes:" -ForegroundColor Yellow
    Write-Host $gitStatus -ForegroundColor Gray
    Write-Host ""
    $response = Read-Host "Continue anyway? (y/N)"
    if ($response -ne 'y' -and $response -ne 'Y') {
        Write-Host "Aborted." -ForegroundColor Red
        exit 1
    }
}

# --- Step 1: Bump version ---
Write-Host "`n--- Step 1: Bump Version ---" -ForegroundColor Cyan

$bumpArg = if ($Patch) { "--patch" } elseif ($Minor) { "--minor" } else { "--major" }

if ($DryRun) {
    Write-Host "[DRY RUN] Would run: .\bump-version.ps1 $bumpArg --tag" -ForegroundColor Gray
} else {
    & "$PSScriptRoot\bump-version.ps1" $bumpArg --tag
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Version bump failed" -ForegroundColor Red
        exit 1
    }
}

# Read new version
$cmakeContent = Get-Content "$PSScriptRoot\CMakeLists.txt" -Raw
if ($cmakeContent -match 'set\(DEFAULT_VERSION\s+"(\d+\.\d+\.\d+)"\)') {
    $newVersion = $matches[1]
} else {
    Write-Host "ERROR: Could not read version from CMakeLists.txt" -ForegroundColor Red
    exit 1
}

Write-Host "Version: v$newVersion" -ForegroundColor Green

# --- Step 2: Commit version bump ---
Write-Host "`n--- Step 2: Commit Version Bump ---" -ForegroundColor Cyan

if ($DryRun) {
    Write-Host "[DRY RUN] Would commit CMakeLists.txt with version bump" -ForegroundColor Gray
} else {
    git add CMakeLists.txt
    git commit -m "chore: bump version to $newVersion"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "WARNING: Commit may have failed (perhaps no changes?)" -ForegroundColor Yellow
    }

    # Move tag to include the commit
    git tag -d "v$newVersion" 2>$null
    git tag -a "v$newVersion" -m "Version $newVersion"
    Write-Host "Tag v$newVersion created on version bump commit" -ForegroundColor Green
}

# --- Step 3: Build (optional) ---
if (-not $SkipBuild) {
    Write-Host "`n--- Step 3: Build Release ---" -ForegroundColor Cyan

    if ($DryRun) {
        Write-Host "[DRY RUN] Would build Release configuration" -ForegroundColor Gray
    } else {
        Write-Host "Building Release..." -ForegroundColor Yellow
        cmake --build build --config Release --target mib_studio_qt
        if ($LASTEXITCODE -ne 0) {
            Write-Host "ERROR: Build failed" -ForegroundColor Red
            exit 1
        }
        Write-Host "Build succeeded" -ForegroundColor Green

        # --- Step 4: Build installers ---
        Write-Host "`n--- Step 4: Build Installers ---" -ForegroundColor Cyan

        Write-Host "Building full installer..." -ForegroundColor Yellow
        cmake --build build --config Release --target package_installer
        if ($LASTEXITCODE -ne 0) {
            Write-Host "WARNING: Full installer build failed" -ForegroundColor Yellow
        } else {
            Write-Host "Full installer built" -ForegroundColor Green
        }

        Write-Host "Building update package..." -ForegroundColor Yellow
        cmake --build build --config Release --target package_installer_update
        if ($LASTEXITCODE -ne 0) {
            Write-Host "WARNING: Update package build failed" -ForegroundColor Yellow
        } else {
            Write-Host "Update package built" -ForegroundColor Green
        }

        # Show output
        Write-Host "`nInstaller output:" -ForegroundColor Cyan
        Get-ChildItem "build\dist\MIB_Studio_Qt_*.exe" -ErrorAction SilentlyContinue | ForEach-Object {
            $hash = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLower().Substring(0, 16)
            Write-Host "  $($_.Name) ($([math]::Round($_.Length / 1MB, 1)) MB) sha256:$hash..." -ForegroundColor Green
        }
    }
} else {
    Write-Host "`n--- Step 3: Build (skipped - CI will handle) ---" -ForegroundColor Cyan
}

# --- Step 5: Push ---
if ($Push) {
    Write-Host "`n--- Step 5: Push to Remote ---" -ForegroundColor Cyan

    if ($DryRun) {
        Write-Host "[DRY RUN] Would push branch and tag v$newVersion" -ForegroundColor Gray
    } else {
        $branch = git rev-parse --abbrev-ref HEAD
        Write-Host "Pushing branch '$branch'..." -ForegroundColor Yellow
        git push origin $branch
        if ($LASTEXITCODE -ne 0) {
            Write-Host "WARNING: Branch push failed" -ForegroundColor Yellow
        }

        Write-Host "Pushing tag v$newVersion..." -ForegroundColor Yellow
        git push origin "v$newVersion"
        if ($LASTEXITCODE -ne 0) {
            Write-Host "ERROR: Tag push failed" -ForegroundColor Red
            exit 1
        }
        Write-Host "Tag pushed - CI/CD release pipeline triggered" -ForegroundColor Green
    }
} else {
    Write-Host "`n--- Step 5: Push (manual) ---" -ForegroundColor Cyan
    Write-Host "To trigger the release pipeline, push the tag:" -ForegroundColor Yellow
    Write-Host "  git push origin main" -ForegroundColor White
    Write-Host "  git push origin v$newVersion" -ForegroundColor White
}

# --- Done ---
Write-Host "`n=== Release Complete ===" -ForegroundColor Cyan
Write-Host "Version: v$newVersion" -ForegroundColor Green

if (-not $SkipBuild) {
    $installers = Get-ChildItem "build\dist\MIB_Studio_Qt_*.exe" -ErrorAction SilentlyContinue
    if ($installers) {
        Write-Host "Installers: $($installers.Count) files in build\dist\" -ForegroundColor Green
    }
}

if ($Push) {
    Write-Host "Status: Pushed - CI/CD release pipeline running" -ForegroundColor Green
    Write-Host "Check: https://github.com/<your-org>/mib-studio-qt/actions" -ForegroundColor Cyan
} else {
    Write-Host "Status: Local release ready. Push tag to trigger CI/CD." -ForegroundColor Yellow
}
