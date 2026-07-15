# Branching model & release pipeline

Two long-lived branches: **`develop`** (integration, default) and **`main`**
(stable / released).

## Flow

```
feature/fix branch ──PR──▶ develop ──PR──▶ main ──tag v*──▶ release
   (checks)            (checks, no review)   (review + checks)
```

- **`develop`** — the default branch and integration line. Cut feature work as
  `feat/*` / `fix/*` branches off `develop` and open a PR back into it.
  - Protected: **required status checks, no required review** — fast iteration
    while staying green.
  - Every merge to `develop` **auto-builds a beta** and publishes it to the R2
    **beta** channel (`build-windows.yml`, `push: [develop]` → beta mode; version
    `X.Y.Z-beta.<sha>`).
- **`main`** — stable, released code. Promote by opening a PR from `develop`
  (or a release branch) into `main`.
  - Protected: **1 review + required status checks**.
  - A **stable** release is cut by dispatching `build-windows.yml` with
    `mode=release` from `main` (bumps the version, tags `vX.Y.Z`, pushes the
    tag, and publishes the stable installer to the R2 stable channel).

## CI triggers

| Workflow | Runs on |
|----------|---------|
| `backend-ci.yml` | push + PR to `main`, `develop` |
| `docs-ci.yml` | push + PR to `main`, `develop` |
| `sanitizers.yml` | PR to `main`, `develop`; nightly; manual |
| `build-windows.yml` | **push to `develop` → auto beta**; manual dispatch (beta any ref / release from `main`) |
| `release.yml` | `v*.*.*` tags; manual |

`build-windows.yml` serializes with a `concurrency` group so rapid `develop`
merges queue rather than race on the R2 publish.

## Rules of thumb

- Don't push directly to `main` or `develop` — always via PR.
- A stable release is only ever cut from `main` (the workflow refuses
  `mode=release` off any other ref).
- Beta versions are immutable per-SHA; the beta channel pointer advances with
  each `develop` merge.
