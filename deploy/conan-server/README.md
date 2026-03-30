# Conan Server Deployment

Self-hosted Conan 2.x package server for caching pre-built C++ dependencies.

Deployed at `https://conan.yofo.bio`, routed via Cloudflare tunnel on the same host as `s3.yofo.bio`.

## Setup

1. Copy this directory to the server at `/home/gavin/Service/conan-server/`
2. Edit `server.conf` — set `jwt_secret` to a random hex string and change the `ci` password
3. Start the server:
   ```bash
   docker compose up -d --build
   ```
4. Add cloudflared ingress rule (in `cloudflared/config.yaml`):
   ```yaml
   - hostname: conan.yofo.bio
     service: http://conan-server:9300
   ```
5. Create DNS route: `cloudflared tunnel route dns <tunnel-id> conan.yofo.bio`

## Client setup

```powershell
.\scripts\setup-conan-remote.ps1
conan remote login team-conan ci
```

## Seeding packages

From the project root on a dev machine:
```powershell
.\scripts\seed-conan-remote.ps1
```
