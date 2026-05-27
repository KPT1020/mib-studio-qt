# Self-hosted Sentry server

This runbook deploys the Sentry server used by MIB Studio Qt crash
reporting. The app-side setup lives in `docs/howto/sentry-setup.md`.

Target:

- Host path: `/home/gavin/Service/sentry/self-hosted`
- Public URL: `https://sentry.yofo.bio`
- Exposure: Cloudflare Tunnel to Sentry's local port `19000`
- App project: Native project `mib-studio-qt` in org `sentry`
- Retention: 90 days

## 1. Host requirements

Use the existing services host pattern already used for local services in this
repo. Sentry's self-hosted stack is substantially larger than the Conan server:

- Docker Engine
- Docker Compose v2
- 4 CPU cores minimum
- 16 GB RAM minimum
- 16 GB swap recommended
- 20 GB disk minimum, with more capacity for 90-day retention

Check the host:

```bash
docker --version
docker compose version
free -h
df -h
```

Create swap if the host does not already have enough:

```bash
sudo fallocate -l 16G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
```

## 2. Install Sentry

Clone the latest official self-hosted release on the service host:

```bash
mkdir -p /home/gavin/Service/sentry
cd /home/gavin/Service/sentry

VERSION=$(curl -Ls -o /dev/null -w %{url_effective} \
  https://github.com/getsentry/self-hosted/releases/latest)
VERSION=${VERSION##*/}

git clone https://github.com/getsentry/self-hosted.git
cd self-hosted
git checkout "$VERSION"

./install.sh --no-report-self-hosted-issues --skip-user-creation
```

Configure the public URL and retention:

```bash
# .env
grep -q '^SENTRY_EVENT_RETENTION_DAYS=' .env \
  && sed -i 's/^SENTRY_EVENT_RETENTION_DAYS=.*/SENTRY_EVENT_RETENTION_DAYS=90/' .env \
  || printf '\nSENTRY_EVENT_RETENTION_DAYS=90\n' >> .env

grep -q '^SENTRY_BIND=' .env \
  && sed -i 's#^SENTRY_BIND=.*#SENTRY_BIND=127.0.0.1:19000#' .env \
  || printf '\nSENTRY_BIND=127.0.0.1:19000\n' >> .env

# sentry/config.yml
grep -q '^system.url-prefix:' sentry/config.yml \
  && sed -i 's#^system.url-prefix:.*#system.url-prefix: "https://sentry.yofo.bio"#' sentry/config.yml \
  || printf '\nsystem.url-prefix: "https://sentry.yofo.bio"\n' >> sentry/config.yml

# Optional: disable Sentry's update beacon.
grep -q '^SENTRY_BEACON = ' sentry/sentry.conf.py \
  && sed -i 's/^SENTRY_BEACON = .*/SENTRY_BEACON = False/' sentry/sentry.conf.py \
  || printf '\nSENTRY_BEACON = False\n' >> sentry/sentry.conf.py

# Required when Sentry sits behind Cloudflare Tunnel / HTTPS reverse proxy.
grep -q '^SECURE_PROXY_SSL_HEADER = ' sentry/sentry.conf.py || cat >> sentry/sentry.conf.py <<'PY'

SECURE_PROXY_SSL_HEADER = ('HTTP_X_FORWARDED_PROTO', 'https')
USE_X_FORWARDED_HOST = True
SESSION_COOKIE_SECURE = True
CSRF_COOKIE_SECURE = True
SOCIAL_AUTH_REDIRECT_IS_HTTPS = True
CSRF_TRUSTED_ORIGINS = ["https://sentry.yofo.bio", "https://sentey.yofo.bio"]
PY
```

Start the stack:

```bash
docker compose up -d --wait
docker compose ps
curl -f http://127.0.0.1:19000/_health/
```

## 3. Cloudflare Tunnel route

Add a Cloudflare Tunnel public hostname:

- Hostname: `sentry.yofo.bio`
- Service: `http://127.0.0.1:19000`

Then verify from outside the host:

```bash
curl -f https://sentry.yofo.bio/_health/
```

If login or project settings show CSRF/origin errors, re-check
`system.url-prefix` and restart the stack:

```bash
docker compose restart web taskworker nginx
```

## 4. Sentry project and GitHub secrets

In `https://sentry.yofo.bio`:

1. Use org `sentry` unless you have intentionally created another org.
2. Create project `mib-studio-qt` with platform `Native`.
3. Copy the project DSN from Project Settings > Client Keys. Store it
   in the GitHub `SENTRY_DSN` secret; do not commit the literal DSN to
   this public repository.
4. Create a user auth token with scopes:
   - `project:read`
   - `project:releases`
   - `project:write`
   - `org:read`

Set these repository Actions secrets:

```text
SENTRY_DSN=https://<public_key>@sentry.yofo.bio/<project_id>
SENTRY_URL=https://sentry.yofo.bio
SENTRY_ORG=sentry
SENTRY_PROJECT=mib-studio-qt
SENTRY_AUTH_TOKEN=<token>
```

Run the Windows release workflow after the secrets are present. The workflow
injects the DSN into the installer, uploads PDB/debug files, and creates a
Sentry release.

## 5. Backups

For a consistent backup, briefly stop the stack and archive every Compose
volume plus the checked-out configuration:

```bash
cd /home/gavin/Service/sentry/self-hosted
backup_dir="/home/gavin/Backups/sentry/$(date +%Y%m%dT%H%M%S)"
mkdir -p "$backup_dir"

docker compose stop
tar -czf "$backup_dir/self-hosted-config.tgz" \
  --exclude .git \
  --exclude sentry/files \
  .

for volume in $(docker compose config --volumes); do
  full_name="$(basename "$PWD")_${volume}"
  docker run --rm \
    -v "${full_name}:/volume:ro" \
    -v "${backup_dir}:/backup" \
    alpine sh -c "cd /volume && tar -czf /backup/${full_name}.tgz ."
done

docker compose up -d --wait
```

Copy the backup directory off-host after the archive completes.

Restore to a fresh host by checking out the same self-hosted release, restoring
the config archive, creating the named volumes, extracting each volume archive
into its matching volume, then starting with `docker compose up -d --wait`.
Perform restore drills on a non-production host before relying on the backup.

## 6. Upgrade

Upgrade during a maintenance window:

```bash
cd /home/gavin/Service/sentry/self-hosted
git fetch --tags origin
VERSION=$(curl -Ls -o /dev/null -w %{url_effective} \
  https://github.com/getsentry/self-hosted/releases/latest)
VERSION=${VERSION##*/}
git checkout "$VERSION"
./install.sh --no-report-self-hosted-issues
docker compose up -d --wait
curl -f https://sentry.yofo.bio/_health/
```

Take a backup before every upgrade.

## 7. Runtime checks

Use these checks after install, upgrade, or incident response:

```bash
cd /home/gavin/Service/sentry/self-hosted
docker compose ps
docker compose logs --tail=200 web taskworker relay nginx
curl -f http://127.0.0.1:19000/_health/
curl -f https://sentry.yofo.bio/_health/
```

For the desktop app, verify an installed machine has `MIB_SENTRY_DSN` set and
that a test crash appears in project `mib-studio-qt` with symbolicated frames.
