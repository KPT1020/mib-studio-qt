## Auto-update via RustFS (S3-compatible)

This app can check a public RustFS (S3-compatible) endpoint for an update manifest, download the latest Windows installer, verify it (SHA-256), and then launch the installer elevated.

### Overview

- **RustFS endpoint**: `https://s3.yofo.bio`
- **Bucket (recommended)**: `mib-studio-qt-updates`
- **Channel prefix (recommended)**: `stable/`
- **Manifest (default URL checked by app)**: `https://s3.yofo.bio/mib-studio-qt-updates/stable/latest.json`
- **Override for testing**: set env var `MIB_STUDIO_UPDATE_MANIFEST_URL` to any HTTPS URL returning a valid manifest JSON.

### Object layout

Store these objects in the bucket:

- `stable/latest.json` (manifest pointing to update package)
- `stable/MIB_Studio_Qt_Update_v<version>.exe` (update package for auto-updates)
- `stable/MIB_Studio_Qt_Setup_v<version>.exe` (optional, full installer for manual downloads)

Example:

- `stable/latest.json`
- `stable/MIB_Studio_Qt_Update_v0.2.0.exe` (used by auto-updater)
- `stable/MIB_Studio_Qt_Setup_v0.2.0.exe` (optional, for manual first-time installs)

**Two installer types:**

1. **Update package** (`MIB_Studio_Qt_Update_v<version>.exe`): App files only, no eGrabber/VC++ redistributable. Smaller (~100-150 MB), faster downloads. Used by auto-updater.
2. **Full installer** (`MIB_Studio_Qt_Setup_v<version>.exe`): Includes app files + optional eGrabber SDK + optional VC++ redistributable. Larger (~200+ MB). For manual first-time installs.

### Manifest format

`stable/latest.json` must be JSON with these fields:

- `version` (string): semantic version like `"0.2.0"`
- `installer_url` (string): HTTPS URL to the installer exe
- `installer_sha256` (string): lowercase or uppercase hex SHA-256 of the installer exe
- `installer_size_bytes` (number): file size in bytes
- `release_notes_url` (optional string): HTTPS URL (e.g., changelog)
- `published_at` (optional string): ISO8601 timestamp

Example manifest (points to update package):

```json
{
  "version": "0.2.0",
  "installer_url": "https://s3.yofo.bio/mib-studio-qt-updates/stable/MIB_Studio_Qt_Update_v0.2.0.exe",
  "installer_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "installer_size_bytes": 123456789,
  "release_notes_url": "https://github.com/your-org/mib-studio-qt/releases/tag/v0.2.0",
  "published_at": "2026-01-21T12:34:56Z"
}
```

**Note:** The `installer_url` should point to the **update package** (not the full installer) for auto-updates. This ensures faster downloads and avoids reinstalling eGrabber/VC++ redistributable on every update.

### Making objects public-read

The app does **not** ship credentials, so `stable/latest.json` and the referenced installer URL must be publicly readable over HTTPS.

**Important:** The `--acl public-read` flag in the publish script may not be sufficient. You may also need to configure a bucket policy.

**Option 1: Bucket Policy (Recommended)**

If your RustFS supports S3 bucket policies, apply a policy that allows public GET for the `stable/` prefix:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Sid": "PublicReadStablePrefix",
      "Effect": "Allow",
      "Principal": "*",
      "Action": ["s3:GetObject"],
      "Resource": ["arn:aws:s3:::mib-studio-qt-updates/stable/*"]
    }
  ]
}
```

Apply via AWS CLI (using the provided `bucket-policy.json` in the repo root):
```powershell
aws --endpoint-url https://s3.yofo.bio --profile rustfs s3api put-bucket-policy --bucket mib-studio-qt-updates --policy file://bucket-policy.json
```

**Note:** The ARN format in the policy (`arn:aws:s3:::mib-studio-qt-updates/stable/*`) is standard S3 format. If RustFS uses a different ARN format, adjust accordingly.

**Option 2: Verify Public Access**

Test if objects are publicly accessible:
```powershell
# Test manifest
Invoke-WebRequest -Uri "https://s3.yofo.bio/mib-studio-qt-updates/stable/latest.json" -Method Head

# Test installer
Invoke-WebRequest -Uri "https://s3.yofo.bio/mib-studio-qt-updates/stable/MIB_Studio_Qt_Update_v0.1.0.exe" -Method Head
```

Both should return `200 OK`, not `403 Forbidden`. If you get `403 Forbidden`, the objects are not publicly accessible and you need to:
1. Check bucket policy settings
2. Verify bucket-level "Block Public Access" settings are disabled for the `stable/` prefix
3. Confirm RustFS supports and respects ACLs/bucket policies

### Publishing a new version (Windows)

1) Build both installers (Inno Setup):

```powershell
# Build full installer (for manual downloads)
cmake --build build --target package_installer --config Release

# Build update package (for auto-updates)
cmake --build build --target package_installer_update --config Release
```

See [`docs/howto/build-installer.md`](docs/howto/build-installer.md) for details.

2) Publish using `publish-update.ps1` (recommended):

This repo includes a PowerShell script at the root (`publish-update.ps1`) that **automates the entire process**: auto-detects version from filename, computes SHA-256, generates `latest.json`, and uploads files using AWS CLI with `public-read` ACL.

**Prerequisites:**

- AWS CLI installed (`aws` available on PATH)
- Credentials configured (via `AWS_ACCESS_KEY_ID`/`AWS_SECRET_ACCESS_KEY` env vars, AWS CLI profiles, or IAM role)

**Usage (update package for auto-updates):**

```powershell
.\publish-update.ps1 `
  -Installer "build\dist\MIB_Studio_Qt_Update_v0.2.0.exe" `
  -Profile rustfs `
  -ReleaseNotesUrl "https://github.com/your-org/mib-studio-qt/releases/tag/v0.2.0"
```

**Usage (full installer for manual downloads, optional):**

```powershell
.\publish-update.ps1 `
  -Installer "build\dist\MIB_Studio_Qt_Setup_v0.2.0.exe" `
  -Profile rustfs
```

**Parameters:**

- `-Installer` (required): Path to the installer `.exe` file (version auto-detected from filename)
- `-Version` (optional): Override version if auto-detection fails
- `-Profile` (optional): AWS CLI profile name (if not using env vars)
- `-Endpoint` (optional, default: `https://s3.yofo.bio`): RustFS endpoint URL
- `-Bucket` (optional, default: `mib-studio-qt-updates`): S3 bucket name
- `-Channel` (optional, default: `stable`): Channel prefix (e.g., `stable`, `beta`)
- `-ReleaseNotesUrl` (optional): URL to release notes/changelog

**Note:** The script always uses `public-read` ACL (no switch needed). Version is automatically extracted from the installer filename if not provided.

The script will:
- Auto-detect version from filename (supports both `Setup` and `Update` patterns)
- Validate the installer file exists
- Compute SHA-256 hash and file size
- Generate `latest.json` manifest with all required fields
- Upload installer to `s3://<bucket>/<channel>/<installer-filename>`
- Upload manifest to `s3://<bucket>/<channel>/latest.json` (with `installer_url` pointing to the uploaded file)
- Display the final manifest and installer URLs

**Important:** For auto-updates, publish the **update package** (`MIB_Studio_Qt_Update_v<version>.exe`). The manifest will point to this smaller package, ensuring faster downloads and avoiding unnecessary reinstalls of eGrabber/VC++ redistributable.

3) Manual upload (alternative, if not using the script):

```powershell
$bucket = "mib-studio-qt-updates"
$version = "0.2.0"
$installerLocal = "build\dist\MIB_Studio_Qt_Update_v$version.exe"  # Use update package for auto-updates
$installerKey = "stable/MIB_Studio_Qt_Update_v$version.exe"
$manifestKey = "stable/latest.json"

aws --endpoint-url "https://s3.yofo.bio" s3api create-bucket --bucket $bucket 2>$null

# Upload installer with public-read ACL
aws --endpoint-url "https://s3.yofo.bio" s3 cp $installerLocal "s3://$bucket/$installerKey" --acl public-read

# Write manifest locally
$hash = (Get-FileHash -Algorithm SHA256 $installerLocal).Hash.ToLower()
$size = (Get-Item $installerLocal).Length
$manifest = @{
  version = $version
  installer_url = "https://s3.yofo.bio/$bucket/$installerKey"
  installer_sha256 = $hash
  installer_size_bytes = $size
  published_at = (Get-Date).ToUniversalTime().ToString("o")
}
$manifest | ConvertTo-Json | Out-File -Encoding utf8 ".\latest.json"

# Upload manifest with public-read ACL
aws --endpoint-url "https://s3.yofo.bio" s3 cp ".\latest.json" "s3://$bucket/$manifestKey" --content-type "application/json" --acl public-read
```

4) Test from a machine that does not have access to your internal network:

- Open in browser: `https://s3.yofo.bio/mib-studio-qt-updates/stable/latest.json`
- Ensure the installer URL downloads without authentication.

### Testing locally (override)

You can point the app to a different manifest URL without rebuilding:

```powershell
$env:MIB_STUDIO_UPDATE_MANIFEST_URL = "https://s3.yofo.bio/mib-studio-qt-updates/stable/latest.json"
```

### Troubleshooting

**403 Forbidden when checking for updates:**

If the app shows "Failed to check for updates" with a 403 error, the manifest/installer files are not publicly accessible:

1. **Verify bucket policy**: Ensure a bucket policy allows public GET for `stable/*` objects
2. **Check ACLs**: Verify objects have `public-read` ACL (the publish script sets this automatically)
3. **Test manually**: Use `Invoke-WebRequest` to test if URLs are accessible without authentication
4. **RustFS-specific**: Some S3-compatible services may require different configuration - check RustFS documentation

**Improved error logging:**

The app now logs detailed error information including HTTP status codes and server responses. Check the application logs for:
- HTTP status code (403 = forbidden, 404 = not found, etc.)
- Server response body (may indicate specific access denied reasons)

### Notes

- The updater downloads the **update package** (app files only), not the full installer.
- The update package is smaller and faster to download (~100-150 MB vs ~200+ MB).
- The update package does not include eGrabber SDK or VC++ redistributable, avoiding unnecessary reinstalls.
- The updater downloads to a temp folder and verifies SHA-256 before launching the installer.
- The updater launches the installer with UAC elevation (Windows) and then closes the app.
- For first-time manual installs, users can download the full installer which includes optional eGrabber/VC++ redistributable components.
