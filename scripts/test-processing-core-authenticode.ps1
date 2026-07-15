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
$signedPath = Join-Path $WorkingDirectory 'mib-processing-authenticode-signed-fixture.exe'

$signerCertificate = $null
try {
    # The SDK binary is Microsoft-signed and already chains to the hosted
    # runner's normal trust roots. That exercises the production verifier
    # without generating certificates or mutating the runner's root store.
    Write-Host 'Locating a pre-trusted x64 Windows SDK signtool'
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
    Copy-Item -LiteralPath $signtool.FullName -Destination $signedPath -Force

    Write-Host 'Reading the embedded SDK signer identity'
    $legacySignerCertificate =
        [System.Security.Cryptography.X509Certificates.X509Certificate]::CreateFromSignedFile(
            $signedPath
        )
    try {
        $signerCertificate =
            [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
                $legacySignerCertificate
            )
    } finally {
        $legacySignerCertificate.Dispose()
    }
    $spki = Get-SignerSpkiSha256 $signerCertificate

    Write-Host 'Running the processing-core Authenticode verifier'
    Invoke-BoundedProcess `
        -FilePath $testPath `
        -ArgumentList @($unsignedPath, $signedPath, $spki) `
        -Description 'Processing-core Authenticode regression test'
} finally {
    if ($signerCertificate) {
        $signerCertificate.Dispose()
    }
    Remove-Item -LiteralPath $signedPath -Force -ErrorAction SilentlyContinue
}
