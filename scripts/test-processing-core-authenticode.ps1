param(
    [Parameter(Mandatory = $true)]
    [string]$TestExecutable,
    [Parameter(Mandatory = $true)]
    [string]$UnsignedFixture,
    [string]$WorkingDirectory = $env:RUNNER_TEMP
)

$ErrorActionPreference = 'Stop'

function Invoke-BoundedProcess(
    [string] $FilePath,
    [string[]] $ArgumentList,
    [string] $Description,
    [int] $TimeoutSeconds = 60
) {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.UseShellExecute = $false
    foreach ($argument in $ArgumentList) {
        [void]$startInfo.ArgumentList.Add($argument)
    }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) {
            throw "$Description did not start"
        }
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill($true)
            throw "$Description exceeded its $TimeoutSeconds-second watchdog"
        }
        if ($process.ExitCode -ne 0) {
            throw "$Description failed with exit code $($process.ExitCode)"
        }
    } finally {
        $process.Dispose()
    }
}

function Get-SignerSpkiSha256(
    [System.Security.Cryptography.X509Certificates.X509Certificate2] $Certificate
) {
    $key = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($Certificate)
    if (-not $key) {
        $key = [System.Security.Cryptography.X509Certificates.ECDsaCertificateExtensions]::GetECDsaPublicKey($Certificate)
    }
    if (-not $key) {
        throw 'The Authenticode test signer uses an unsupported public-key algorithm'
    }
    try {
        $subjectPublicKeyInfo = $key.ExportSubjectPublicKeyInfo()
    } finally {
        $key.Dispose()
    }
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $digest = $sha256.ComputeHash($subjectPublicKeyInfo)
    } finally {
        $sha256.Dispose()
    }
    return (($digest | ForEach-Object { $_.ToString('x2') }) -join '')
}

$testPath = (Resolve-Path -LiteralPath $TestExecutable).Path
$unsignedPath = (Resolve-Path -LiteralPath $UnsignedFixture).Path
New-Item -ItemType Directory -Path $WorkingDirectory -Force | Out-Null
$signedPath = Join-Path $WorkingDirectory 'mib-processing-authenticode-signed-fixture.dll'
$pfxPath = Join-Path $WorkingDirectory 'mib-processing-authenticode-ci-fixture.pfx'
$cerPath = Join-Path $WorkingDirectory 'mib-processing-authenticode-ci-fixture.cer'
Copy-Item -LiteralPath $unsignedPath -Destination $signedPath -Force

$certificate = $null
$rootInstalled = $false
try {
    # These are deliberately public, test-only credentials. Keeping the fixture
    # static avoids hosted-runner certificate-provider hangs; production signing
    # continues to use repository secrets in a separate release job.
    Write-Host 'Materializing the public Authenticode CI fixture'
    $repositoryRoot = Split-Path -Parent $PSScriptRoot
    $fixtureRoot = Join-Path $repositoryRoot 'tests\processing\fixtures'
    $pfxBase64Path = Join-Path $fixtureRoot 'authenticode_ci_fixture.pfx.b64'
    $cerBase64Path = Join-Path $fixtureRoot 'authenticode_ci_fixture.cer.b64'
    $pfxPassword = 'mib-processing-ci-fixture'
    [IO.File]::WriteAllBytes(
        $pfxPath,
        [Convert]::FromBase64String((Get-Content -LiteralPath $pfxBase64Path -Raw))
    )
    [IO.File]::WriteAllBytes(
        $cerPath,
        [Convert]::FromBase64String((Get-Content -LiteralPath $cerBase64Path -Raw))
    )
    $certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($cerPath)

    Write-Host 'Installing the CI fixture in the current-user trust store'
    Invoke-BoundedProcess `
        -FilePath 'certutil.exe' `
        -ArgumentList @('-user', '-f', '-addstore', 'Root', $cerPath) `
        -Description 'Current-user CI root installation'
    $rootInstalled = $true

    Write-Host 'Locating the x64 Windows SDK signtool'
    $signtoolPattern = Join-Path `
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin" `
        '*\x64\signtool.exe'
    $signtool = Get-ChildItem -Path $signtoolPattern -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $signtool) {
        throw 'signtool.exe (x64) was not found'
    }

    Write-Host 'Signing the DLL with the public CI certificate fixture'
    Invoke-BoundedProcess `
        -FilePath $signtool.FullName `
        -ArgumentList @('sign', '/fd', 'SHA256', '/f', $pfxPath, '/p', $pfxPassword, $signedPath) `
        -Description 'Ephemeral signtool'
    $spki = Get-SignerSpkiSha256 $certificate

    Write-Host 'Running the processing-core Authenticode verifier'
    Invoke-BoundedProcess `
        -FilePath $testPath `
        -ArgumentList @($unsignedPath, $signedPath, $spki) `
        -Description 'Processing-core Authenticode regression test'
} finally {
    if ($rootInstalled -and $certificate) {
        Write-Host 'Removing the CI fixture from the current-user trust store'
        Invoke-BoundedProcess `
            -FilePath 'certutil.exe' `
            -ArgumentList @('-user', '-delstore', 'Root', $certificate.Thumbprint) `
            -Description 'Current-user CI root removal'
    }
    if ($certificate) {
        $certificate.Dispose()
    }
    Remove-Item -LiteralPath $pfxPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $cerPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $signedPath -Force -ErrorAction SilentlyContinue
}
