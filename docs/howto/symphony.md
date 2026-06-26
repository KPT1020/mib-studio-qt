# OpenAI Symphony setup

This repo has a repository-owned Symphony workflow at `WORKFLOW.md`.
Symphony is an OpenAI engineering-preview orchestrator that polls Linear,
creates one workspace per issue, and runs Codex in `app-server` mode.

## Local prerequisites

- Codex CLI with `codex app-server` available.
- Git.
- `mise` for the OpenAI Elixir reference implementation.
- A Linear personal API key exported as `LINEAR_API_KEY`.

On Windows, install `mise` with:

```powershell
winget install jdx.mise
```

Restart PowerShell after installing `mise`.

## Linear project

The workflow is configured for the existing Linear project:

- Team: `Kingsphase`
- Project: `mib-studio`
- Project URL slug: `mib-studio-97e2c43e910d`

The default active states are `Todo`, `In Progress`, and `Rework`.
The default terminal states are `Done`, `Closed`, `Cancelled`, `Canceled`,
and `Duplicate`.

## Start Symphony

From the repository root:

```powershell
$env:LINEAR_API_KEY = "<linear personal api key>"
.\scripts\start-symphony.ps1
```

The wrapper clones or reuses `openai/symphony` under
`%LOCALAPPDATA%\OpenAI\Symphony\source`, sets
`SYMPHONY_WORKSPACE_ROOT` to
`%USERPROFILE%\Developer\symphony-workspaces\mib-studio`, builds the Elixir
reference implementation, and starts the dashboard on port `4402`.
It also passes Symphony's required high-trust preview acknowledgement flag.

To update the local Symphony checkout before starting:

```powershell
.\scripts\start-symphony.ps1 -UpdateSymphony
```

To keep a startup transcript:

```powershell
.\scripts\start-symphony.ps1 -LogPath data\logs\symphony-startup.log
```

## Operational notes

- Symphony is intended for trusted environments. Do not point it at issues from
  untrusted authors without adding stronger sandboxing and review gates.
- Per-issue workspaces are outside this repository by default so agent runs do
  not modify the operator checkout.
- `WORKFLOW.md` requires agents to update the vault and run the docs checker
  when they make code or docs changes.
- The workflow leaves issues ready for human review; it does not instruct
  agents to mark work done automatically.
