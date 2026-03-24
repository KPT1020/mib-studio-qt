# Unified release script for MIB Studio Qt
# Builds locally, creates GitHub Release, and publishes to RustFS — all from your machine.
#
# Usage:
#   .\release.ps1 --patch                    # Production: bump, build, tag (v0.2.2)
#   .\release.ps1 --patch --push             # Production: bump, build, tag, push, create GitHub Release, publish to stable
#   .\release.ps1 --patch --beta             # Test: bump, build, tag as v0.2.2-beta.1
#   .\release.ps1 --patch --beta --push      # Test: bump, build, tag, push, create GitHub pre-release, publish to test
#   .\release.ps1 --patch --skip-build --push # Bump, tag, push (skip build + publish)
#   .\release.ps1 --patch --dry-run          # Preview what would happen

param(
    [switch]$Patch,
    [switch]$Minor,
    [switch]$Major,
    [switch]$Beta,
    [switch]$Push,
    [switch]$SkipBuild,
    [switch]$DryRun,
    [string]$Profile = "rustfs"
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
    Write-Host "Usage: .\release.ps1 --patch|--minor|--major [--beta] [--push] [--skip-build] [--dry-run]" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Options:" -ForegroundColor Yellow
    Write-Host "  --patch       Bump patch version (bug fixes)"
    Write-Host "  --minor       Bump minor version (new features)"
    Write-Host "  --major       Bump major version (breaking changes)"
    Write-Host "  --beta        Create a test/pre-release (publishes to test channel)"
    Write-Host "  --push        Push tag, create GitHub Release, publish to RustFS"
    Write-Host "  --skip-build  Skip local build (tag and push only)"
    Write-Host "  --dry-run     Show what would happen without making changes"
    Write-Host "  --profile     AWS CLI profile for RustFS (default: rustfs)"
    exit 1
}

# --- Check prerequisites ---
Write-Host "=== MIB Studio Qt Release ===" -ForegroundColor Cyan

if ($Push -and -not $SkipBuild) {
    # Check gh CLI is available
    try {
        $null = gh --version 2>&1
    } catch {
        Write-Host "ERROR: 'gh' CLI not found. Install from https://cli.github.com/" -ForegroundColor Red
        exit 1
    }
}

# --- Check git status ---
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
    $bumpParams = @{ Tag = $true }
    if ($Patch) { $bumpParams.Patch = $true }
    elseif ($Minor) { $bumpParams.Minor = $true }
    else { $bumpParams.Major = $true }
    & "$PSScriptRoot\bump-version.ps1" @bumpParams
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

# Determine tag name based on channel
if ($Beta) {
    # Find next beta number for this version
    $betaNum = 1
    $existingBetaTags = git tag -l "v$newVersion-beta.*" 2>$null
    if ($existingBetaTags) {
        $existingBetaTags -split "`n" | ForEach-Object {
            if ($_ -match "v$([regex]::Escape($newVersion))-beta\.(\d+)") {
                $num = [int]$matches[1]
                if ($num -ge $betaNum) { $betaNum = $num + 1 }
            }
        }
    }
    $tagName = "v$newVersion-beta.$betaNum"
    $channel = "test"
} else {
    $tagName = "v$newVersion"
    $channel = "stable"
}

Write-Host "Version: $tagName (channel: $channel)" -ForegroundColor Green

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

    # Move tag to include the commit (only for non-beta; beta tags are always new)
    if (-not $Beta) {
        git tag -d "v$newVersion" 2>$null
    }
    git tag -a "$tagName" -m "Version $tagName"
    Write-Host "Tag $tagName created on version bump commit" -ForegroundColor Green
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
    }

    # --- Step 4: Build installers ---
    Write-Host "`n--- Step 4: Build Installers ---" -ForegroundColor Cyan

    if ($DryRun) {
        Write-Host "[DRY RUN] Would build installers" -ForegroundColor Gray
    } else {
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
    Write-Host "`n--- Step 3-4: Build (skipped) ---" -ForegroundColor Cyan
}

# --- Step 5: Push + Publish ---
if ($Push) {
    Write-Host "`n--- Step 5: Push to Remote ---" -ForegroundColor Cyan

    if ($DryRun) {
        Write-Host "[DRY RUN] Would push branch and tag $tagName" -ForegroundColor Gray
    } else {
        $branch = git rev-parse --abbrev-ref HEAD
        Write-Host "Pushing branch '$branch'..." -ForegroundColor Yellow
        git push origin $branch
        if ($LASTEXITCODE -ne 0) {
            Write-Host "WARNING: Branch push failed" -ForegroundColor Yellow
        }

        Write-Host "Pushing tag $tagName..." -ForegroundColor Yellow
        git push origin "$tagName"
        if ($LASTEXITCODE -ne 0) {
            Write-Host "ERROR: Tag push failed" -ForegroundColor Red
            exit 1
        }
        Write-Host "Tag pushed" -ForegroundColor Green
    }

    # --- Step 6: Create GitHub Release ---
    if (-not $SkipBuild) {
        Write-Host "`n--- Step 6: Create GitHub Release ---" -ForegroundColor Cyan

        $setupExe = Get-ChildItem "build\dist\MIB_Studio_Qt_Setup_v*.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
        $updateExe = Get-ChildItem "build\dist\MIB_Studio_Qt_Update_v*.exe" -ErrorAction SilentlyContinue | Select-Object -First 1

        if (-not $setupExe -and -not $updateExe) {
            Write-Host "WARNING: No installer files found in build\dist\, skipping GitHub Release" -ForegroundColor Yellow
        } else {
            # Build checksums for release body
            $checksumLines = ""
            $releaseFiles = @()
            if ($setupExe) {
                $setupHash = (Get-FileHash -Algorithm SHA256 $setupExe.FullName).Hash.ToLower()
                $checksumLines += "$setupHash  $($setupExe.Name)`n"
                $releaseFiles += $setupExe.FullName
            }
            if ($updateExe) {
                $updateHash = (Get-FileHash -Algorithm SHA256 $updateExe.FullName).Hash.ToLower()
                $checksumLines += "$updateHash  $($updateExe.Name)`n"
                $releaseFiles += $updateExe.FullName
            }

            $releaseBody = @"
## MIB Studio Qt $tagName

**Channel:** ``$channel``

### Downloads
- **Full Installer** - For first-time installations (includes eGrabber SDK + VC++ Redistributable)
- **Update Package** - For existing installations (app files only, smaller download)

### Checksums (SHA-256)
``````
$checksumLines``````
"@

            $ghArgs = @("release", "create", "$tagName", "--title", "MIB Studio Qt $tagName", "--notes", $releaseBody)
            if ($Beta) {
                $ghArgs += "--prerelease"
            }
            $ghArgs += $releaseFiles

            if ($DryRun) {
                Write-Host "[DRY RUN] Would create GitHub Release for $tagName with $($releaseFiles.Count) files" -ForegroundColor Gray
            } else {
                Write-Host "Creating GitHub Release for $tagName..." -ForegroundColor Yellow
                & gh @ghArgs
                if ($LASTEXITCODE -ne 0) {
                    Write-Host "WARNING: GitHub Release creation failed" -ForegroundColor Yellow
                } else {
                    Write-Host "GitHub Release created" -ForegroundColor Green
                }
            }
        }

        # --- Step 7: Publish to RustFS ---
        Write-Host "`n--- Step 7: Publish to RustFS ($channel) ---" -ForegroundColor Cyan

        if ($updateExe) {
            if ($DryRun) {
                Write-Host "[DRY RUN] Would publish $($updateExe.Name) to $channel channel" -ForegroundColor Gray
            } else {
                Write-Host "Publishing to $channel channel..." -ForegroundColor Yellow
                & "$PSScriptRoot\publish-update.ps1" `
                    -Installer $updateExe.FullName `
                    -Channel $channel `
                    -Profile $Profile `
                    -ReleaseNotesUrl "https://github.com/gavinlouuu-kpt/mib-studio-qt/releases/tag/$tagName"
                if ($LASTEXITCODE -ne 0) {
                    Write-Host "WARNING: RustFS publish failed" -ForegroundColor Yellow
                } else {
                    Write-Host "Published to RustFS ($channel)" -ForegroundColor Green
                }
            }
        } else {
            Write-Host "WARNING: Update package not found, skipping RustFS publish" -ForegroundColor Yellow
        }
    } else {
        Write-Host "`n--- Step 6-7: Publish (skipped - no build) ---" -ForegroundColor Cyan
    }
} else {
    Write-Host "`n--- Step 5: Push (manual) ---" -ForegroundColor Cyan
    Write-Host "To publish this release:" -ForegroundColor Yellow
    Write-Host "  .\release.ps1 --push   (or push manually below)" -ForegroundColor White
    Write-Host "  git push origin main" -ForegroundColor White
    Write-Host "  git push origin $tagName" -ForegroundColor White
}

# --- Done ---
Write-Host "`n=== Release Complete ===" -ForegroundColor Cyan
Write-Host "Version: $tagName (channel: $channel)" -ForegroundColor Green

if (-not $SkipBuild) {
    $installers = Get-ChildItem "build\dist\MIB_Studio_Qt_*.exe" -ErrorAction SilentlyContinue
    if ($installers) {
        Write-Host "Installers: $($installers.Count) files in build\dist\" -ForegroundColor Green
    }
}

if ($Push -and -not $SkipBuild) {
    Write-Host "Channel: $channel" -ForegroundColor Green
    Write-Host "Status: Released (GitHub Release + RustFS)" -ForegroundColor Green
} elseif ($Push) {
    Write-Host "Status: Tag pushed (no installers published)" -ForegroundColor Yellow
} else {
    Write-Host "Status: Local only. Run with --push to publish." -ForegroundColor Yellow
}
