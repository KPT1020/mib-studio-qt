param(
    [string]$Workflow = "WORKFLOW.md",
    [string]$SymphonyDir = "$env:LOCALAPPDATA\OpenAI\Symphony\source",
    [string]$WorkspaceRoot = "$env:USERPROFILE\Developer\symphony-workspaces\mib-studio",
    [int]$Port = 4402,
    [string]$LogPath = "",
    [switch]$UpdateSymphony
)

$ErrorActionPreference = "Stop"

if ($LogPath) {
    $logDir = Split-Path -Parent $LogPath
    if ($logDir) {
        New-Item -ItemType Directory -Force -Path $logDir | Out-Null
    }
    Start-Transcript -Path $LogPath -Append | Out-Null
}

try {

function Require-Command {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [string]$InstallHint
    )

    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Missing required command '$Name'. $InstallHint"
    }
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$workflowPath = Resolve-Path (Join-Path $repoRoot $Workflow)

$wingetMise = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages\jdx.mise_Microsoft.Winget.Source_8wekyb3d8bbwe\mise\bin"
if ((Test-Path $wingetMise) -and ($env:PATH -notlike "*$wingetMise*")) {
    $env:PATH = "$wingetMise;$env:PATH"
}

$gitShell = "C:\Program Files\Git\bin"
if ((Test-Path $gitShell) -and ($env:PATH -notlike "*$gitShell*")) {
    $env:PATH = "$gitShell;$env:PATH"
}

Require-Command -Name "git" -InstallHint "Install Git for Windows and retry."
Require-Command -Name "codex" -InstallHint "Install or sign in to Codex CLI and retry."
Require-Command -Name "mise" -InstallHint "Install mise from https://mise.jdx.dev/ or with 'winget install jdx.mise', then restart PowerShell."
Require-Command -Name "sh" -InstallHint "Install Git for Windows or add its bin directory to PATH and retry."
Require-Command -Name "bash" -InstallHint "Install Git for Windows or add its bin directory to PATH and retry."

if (-not $env:LINEAR_API_KEY) {
    $linearApiKey = [Environment]::GetEnvironmentVariable("LINEAR_API_KEY", "User")
    if (-not $linearApiKey) {
        $linearApiKey = [Environment]::GetEnvironmentVariable("LINEAR_API_KEY", "Machine")
    }
    if ($linearApiKey) {
        $env:LINEAR_API_KEY = $linearApiKey
    } else {
        throw "LINEAR_API_KEY is not set. Create a Linear personal API key and set it before starting Symphony."
    }
}

if (-not (Test-Path $SymphonyDir)) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $SymphonyDir) | Out-Null
    git clone https://github.com/openai/symphony.git $SymphonyDir
} elseif ($UpdateSymphony) {
    git -C $SymphonyDir pull --ff-only
}

$elixirDir = Join-Path $SymphonyDir "elixir"
if (-not (Test-Path $elixirDir)) {
    throw "Expected Symphony Elixir directory at '$elixirDir'."
}

New-Item -ItemType Directory -Force -Path $WorkspaceRoot | Out-Null
$env:SYMPHONY_WORKSPACE_ROOT = $WorkspaceRoot

Push-Location $elixirDir
try {
    mise trust
    mise install
    mise exec -- mix setup
    mise exec -- mix build
    mise exec -- escript ./bin/symphony "$workflowPath" --port $Port --i-understand-that-this-will-be-running-without-the-usual-guardrails
} finally {
    Pop-Location
}
} finally {
    if ($LogPath) {
        Stop-Transcript | Out-Null
    }
}
