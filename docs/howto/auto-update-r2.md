## Auto-update via Cloudflare R2

MIB Studio checks a public update manifest, downloads the latest Windows update package, verifies SHA-256, and launches the installer elevated. Production update artifacts are hosted in a dedicated Cloudflare R2 bucket and exposed through the public custom hostname `https://updates.yofo.bio`.

### Overview

- **Public update hostname**: `https://updates.yofo.bio`
- **R2 bucket**: `mib-studio-qt-updates`
- **R2 S3 API endpoint**: set locally with `MIB_STUDIO_R2_ENDPOINT` (`https://<account-id>.r2.cloudflarestorage.com`)
- **Channel prefixes**: `stable/` for production, `beta/` for beta/pre-release builds
- **Production manifest**: `https://updates.yofo.bio/stable/latest.json`
- **Override for testing**: set `MIB_STUDIO_UPDATE_MANIFEST_URL` to any HTTPS URL returning a compatible manifest JSON

Do not commit R2 credentials or account IDs that are intended to remain private. Use a logged-in Wrangler session, local environment variables, an AWS CLI profile, CI secrets, or Cloudflare-managed credentials.

### Object Layout

Keep objects at the root of the public custom domain:

- `stable/latest.json`
- `stable/index.json` — full version history for the channel (see below)
- `stable/MIB_Studio_Qt_Update_v<version>.exe`
- `stable/MIB_Studio_Qt_Setup_v<version>.exe` (optional, full installer for manual downloads)
- `stable/tools/tools-latest.json`
- `stable/tools/MIB_Studio_Tools_v<version>_windows.zip`
- `beta/...` for beta/pre-release equivalents

#### `index.json` (version catalog)

`{channel}/index.json` lists every published version so the in-app **Help ▸
Software Updates…** dialog can offer channel + specific-version selection
(including rollback). `latest.json` is unchanged and still drives the silent
auto-check; `index.json` is additive.

```json
{
  "schema_version": 1,
  "channel": "beta",
  "versions": [
    {
      "version": "1.0.4-beta.1",
      "installer_url": "https://updates.yofo.bio/beta/MIB_Studio_Qt_Update_v1.0.4-beta.1.exe",
      "installer_sha256": "…",
      "installer_size_bytes": 12345678,
      "release_notes_url": "https://github.com/KPT1020/mib-studio-qt/releases/tag/v1.0.4-beta.1",
      "published_utc": "2026-06-24T04:54:58Z"
    }
  ]
}
```

Newest-first; a release sorts above its own betas. `publish-update.py` maintains
it automatically on every publish (see *Publishing a New App Version*). The app
parses it with `UpdateCatalog`; entries missing `version`/`installer_url`/
`installer_sha256` are skipped, and a missing/invalid `index.json` degrades to
"use Check for Latest" without affecting the auto-check.

### Profile Catalogs

Public profile catalogs live under `profiles/<channel>/` and are read
without authentication:

- `profiles/stable/catalog.json`
- `profiles/stable/<profile-id>/profile.meta.json`
- `profiles/stable/<profile-id>/config.json`
- `profiles/stable/<profile-id>/egrabberConfig.js`

The app checks these only when the user requests it from `ConfigTabs`.
Catalogs and mutable per-profile metadata should use short cache lifetimes;
versioned immutable objects can be cached longer if they are added later.

### Young's Modulus LUT

The Young's modulus LUT is managed separately from app installers and follows
the same operational pattern: publish a public manifest, download/update into
a user-writable cache, verify SHA-256, and fall back to the bundled copy when
offline or incompatible.

Public object layout:

- `stable/emodulus-lut/latest.json`
- `stable/emodulus-lut/scaled_isoelastic_data_LUT_6.16-4.24.txt`
- `beta/emodulus-lut/...` for beta/testing LUTs if you need a separate track

Manifest format:

- `manifest_schema_version` (number): currently `1`
- `lut_id` (string): stable LUT identifier
- `display_name` (string): human-friendly label
- `revision` (string): published LUT revision
- `download_url` (string): HTTPS or `file://` URL to the LUT payload
- `sha256` (string): lowercase or uppercase hex SHA-256 of the LUT file
- `size_bytes` (number): file size in bytes
- `published_at` (optional string): ISO8601 timestamp
- `app_min_version` / `app_max_version` (optional strings): compatibility bounds

Example:

```json
{
  "manifest_schema_version": 1,
  "lut_id": "scaled_isoelastic_data_LUT_6.16-4.24",
  "display_name": "Scaled Isoelastic LUT",
  "revision": "2026.06.11-1",
  "download_url": "https://updates.yofo.bio/stable/emodulus-lut/scaled_isoelastic_data_LUT_6.16-4.24.txt",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "size_bytes": 1234567,
  "published_at": "2026-06-11T12:34:56Z"
}
```

Publishing:

```bash
python publish-emodulus-lut.py \
  --lut "resources/isoelastic_curve/scaled_isoelastic_data_LUT_6.16-4.24.txt" \
  --revision "2026.06.11-1"
```

Verification:

```bash
python verify-emodulus-lut-manifest.py
```

Runtime behavior:

- New builds check `MIB_STUDIO_EMODULUS_LUT_MANIFEST_URL` first; otherwise they
  use `https://updates.yofo.bio/stable/emodulus-lut/latest.json`.
- Downloaded LUTs are cached under the user-local app data tree, in
  `isoelastic_curve/scaled_isoelastic_data_LUT_6.16-4.24.txt` by default.
- `MIB_STUDIO_EMODULUS_LUT_CACHE_DIR` can redirect the cache for testing.
- If the manifest fetch fails, the app keeps the last known-good local copy or
  falls back to the bundled LUT on first run/offline launches.

Rollback:

1. Publish a corrected `stable/emodulus-lut/latest.json` that points to the
   last known-good LUT.
2. Verify with `python verify-emodulus-lut-manifest.py`.
3. If the cache is corrupted locally, delete the LUT cache directory to force a
   fresh seed from the bundled copy or the next successful remote manifest.

Example public URLs:

```text
https://updates.yofo.bio/profiles/stable/catalog.json
https://updates.yofo.bio/profiles/stable/lab-default/profile.meta.json
https://updates.yofo.bio/profiles/stable/lab-default/config.json
https://updates.yofo.bio/profiles/stable/lab-default/egrabberConfig.js
```

The public URL is `https://updates.yofo.bio/<object-key>`. Do not include the bucket name in public manifest URLs when using the R2 custom domain.

### Manifest Format

`stable/latest.json` must be JSON with these fields:

- `version` (string): semantic version like `"0.2.0"`
- `installer_url` (string): HTTPS URL to the update package
- `installer_sha256` (string): lowercase or uppercase hex SHA-256 of the installer exe
- `installer_size_bytes` (number): file size in bytes
- `release_notes_url` (optional string): HTTPS URL, usually the GitHub release
- `published_at` (optional string): ISO8601 timestamp

Example:

```json
{
  "version": "0.2.0",
  "installer_url": "https://updates.yofo.bio/stable/MIB_Studio_Qt_Update_v0.2.0.exe",
  "installer_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "installer_size_bytes": 123456789,
  "release_notes_url": "https://github.com/gavinlouuu-kpt/mib-studio-qt/releases/tag/v0.2.0",
  "published_at": "2026-01-21T12:34:56Z"
}
```

The `installer_url` should point to `MIB_Studio_Qt_Update_v<version>.exe` for auto-updates. Publish the full setup installer separately only when manual first-time downloads need it.

### Cloudflare Configuration

Configure these outside the repo:

1. Create a dedicated R2 bucket named `mib-studio-qt-updates`.
2. Attach the public custom domain `updates.yofo.bio` to that bucket.
3. Configure public read access through Cloudflare for update artifacts.
4. Configure cache behavior:
   - `stable/latest.json`, `beta/latest.json`, and `*/tools/tools-latest.json`: short TTL or bypass cache because manifests are mutable.
   - Versioned `.exe` and `.zip` artifacts: long TTL because filenames are immutable.
5. Create least-privilege write credentials for release publishing. Credentials need object write access to the updater bucket only.
6. Store credentials in a local AWS profile such as `mib-studio-r2`, environment variables, or CI secrets.

Recommended S3-compatible local environment:

```bash
export MIB_STUDIO_R2_ENDPOINT="https://<account-id>.r2.cloudflarestorage.com"
export MIB_STUDIO_R2_PROFILE="mib-studio-r2"
```

If `MIB_STUDIO_R2_ENDPOINT` is not set, the Python publish scripts use `wrangler r2 object put --remote` with the currently authenticated Wrangler session.

### Migration From RustFS

Preserve the existing channel layout when copying objects from the old RustFS bucket:

```bash
export MIB_STUDIO_R2_ENDPOINT="https://<account-id>.r2.cloudflarestorage.com"

aws --endpoint-url https://s3.yofo.bio --profile rustfs s3 sync s3://mib-studio-qt-updates/stable ./tmp-updates/stable
aws --endpoint-url "$MIB_STUDIO_R2_ENDPOINT" --profile mib-studio-r2 s3 sync ./tmp-updates/stable s3://mib-studio-qt-updates/stable

aws --endpoint-url https://s3.yofo.bio --profile rustfs s3 sync s3://mib-studio-qt-updates/beta ./tmp-updates/beta
aws --endpoint-url "$MIB_STUDIO_R2_ENDPOINT" --profile mib-studio-r2 s3 sync ./tmp-updates/beta s3://mib-studio-qt-updates/beta
```

After copying, update migrated manifests if they still point at `https://s3.yofo.bio/mib-studio-qt-updates/...`. The app expects the manifest's `installer_url` to be publicly downloadable without credentials.

### Publishing a New App Version

Build both installers:

```bash
cmake --build build --target package_installer --config Release
cmake --build build --target package_installer_update --config Release
```

Publish the update package:

```bash
python publish-update.py \
  --installer "build/dist/MIB_Studio_Qt_Update_v0.2.0.exe" \
  --release-notes-url "https://github.com/gavinlouuu-kpt/mib-studio-qt/releases/tag/v0.2.0"
```

Publish the optional full installer:

```bash
python publish-update.py --installer "build/dist/MIB_Studio_Qt_Setup_v0.2.0.exe"
```

`publish-update.py` uploads to `s3://mib-studio-qt-updates/<channel>/...`, generates `<channel>/latest.json`, **updates `<channel>/index.json`** (fetches the current index, inserts the new version via `merge_index` — dedupe by version, newest-first — and re-uploads; an empty index self-seeds with the prior `latest.json` so it isn't just the version being published), and prints final public URLs under `https://updates.yofo.bio`. It uses S3/boto3 when `MIB_STUDIO_R2_ENDPOINT` is set, otherwise Wrangler. `publish-update.ps1` is a Windows compatibility wrapper around the Python command.

The `index.json` accumulates from each publish onward; to seed deeper history, re-publish older update packages (idempotent — `merge_index` dedupes) or hand-edit `index.json` and re-upload.

For legacy S3-compatible targets that require object ACLs, pass `--acl public-read`. R2 public access is configured at the bucket/custom-domain layer, so ACLs are not sent by default.

### Publishing Tools

```bash
python publish-tools.py --zip "tools/dist/MIB_Studio_Tools_v0.1.7_windows.zip"
```

The tools manifest is published to `https://updates.yofo.bio/stable/tools/tools-latest.json`.

### Verification

Run the public manifest verifier after publishing:

```bash
python verify-update-manifest.py
```

For beta channel smoke tests:

```bash
python verify-update-manifest.py --manifest-url "https://updates.yofo.bio/beta/latest.json"
```

Manual checks:

```powershell
Invoke-WebRequest -Uri "https://updates.yofo.bio/stable/latest.json" -Method Head
$manifest = Invoke-WebRequest -Uri "https://updates.yofo.bio/stable/latest.json" | ConvertFrom-Json
Invoke-WebRequest -Uri $manifest.installer_url -Method Head
```

Local app smoke test:

```powershell
$env:MIB_STUDIO_UPDATE_MANIFEST_URL = "https://updates.yofo.bio/stable/latest.json"
```

Then launch the app and use Help -> Check for Updates.

### Legacy Client Compatibility

`s3.yofo.bio` is **retired** — releases publish only to `https://updates.yofo.bio`
(the compiled default since PR #169). Do not reintroduce an `s3.yofo.bio`
manifest or redirect.

Clients built before PR #169 still request
`https://s3.yofo.bio/mib-studio-qt-updates/stable/latest.json` and will **not**
auto-update. Upgrade them manually by running the current full installer once;
afterwards they track `updates.yofo.bio` like every other client.

### Rollback

If a bad R2 release is published:

1. Generate and publish a corrected `stable/latest.json` that points to the last known-good update package.
2. Verify with `python verify-update-manifest.py`.
3. If R2 public access is unhealthy, set `MIB_STUDIO_UPDATE_MANIFEST_URL` for smoke tests or publish a temporary manifest on a known-good HTTPS endpoint.
4. Confirm clients pick up the corrected `updates.yofo.bio` manifest (no legacy `s3.yofo.bio` endpoint is involved).

### Troubleshooting

**Manifest returns 403 or 404**

- Confirm `updates.yofo.bio` is attached to the correct R2 bucket.
- Confirm public read access is enabled for the bucket/custom domain.
- Confirm the object key is `<channel>/latest.json` and does not include the bucket name.

**Manifest is stale after publishing**

- Check Cloudflare cache rules for mutable manifest paths.
- Purge cache for `https://updates.yofo.bio/<channel>/latest.json` if needed.

**Upload fails with credentials or endpoint errors**

- Confirm `MIB_STUDIO_R2_ENDPOINT` is set to the account-specific R2 S3 API endpoint.
- Confirm `MIB_STUDIO_R2_PROFILE` points to a profile with write access to `mib-studio-qt-updates`.
- Avoid committing access keys or endpoint-specific secrets to the repo.
