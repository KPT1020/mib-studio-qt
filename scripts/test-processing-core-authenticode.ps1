param(
    [Parameter(Mandatory = $true)]
    [string]$TestExecutable,
    [Parameter(Mandatory = $true)]
    [string]$UnsignedFixture,
    [string]$WorkingDirectory = $env:RUNNER_TEMP
)

$ErrorActionPreference = 'Stop'

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
Copy-Item -LiteralPath $unsignedPath -Destination $signedPath -Force

$certificate = $null
$rootStore = $null
try {
    Write-Host 'Creating ephemeral Authenticode signer'
    $certificate = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject 'CN=MIB Processing Core Ephemeral CI Fixture' `
        -CertStoreLocation 'Cert:\CurrentUser\My' `
        -KeyAlgorithm RSA `
        -KeyLength 2048 `
        -KeyExportPolicy Exportable `
        -NotAfter (Get-Date).AddDays(1)

    $rootStore = [System.Security.Cryptography.X509Certificates.X509Store]::new(
        [System.Security.Cryptography.X509Certificates.StoreName]::Root,
        [System.Security.Cryptography.X509Certificates.StoreLocation]::CurrentUser
    )
    $rootStore.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
    $rootStore.Add($certificate)

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

    Write-Host 'Signing the fixture with the ephemeral certificate'
    & $signtool.FullName sign /fd SHA256 /sha1 $certificate.Thumbprint /s My $signedPath
    if ($LASTEXITCODE -ne 0) {
        throw "Ephemeral signtool failed with exit code $LASTEXITCODE"
    }
    $spki = Get-SignerSpkiSha256 $certificate

    Write-Host 'Running the processing-core Authenticode verifier'
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $testPath
    $startInfo.UseShellExecute = $false
    foreach ($argument in @($unsignedPath, $signedPath, $spki)) {
        [void]$startInfo.ArgumentList.Add($argument)
    }
    $testProcess = [System.Diagnostics.Process]::new()
    $testProcess.StartInfo = $startInfo
    if (-not $testProcess.Start()) {
        throw 'Processing-core Authenticode regression test did not start'
    }
    try {
        if (-not $testProcess.WaitForExit(60000)) {
            $testProcess.Kill($true)
            throw 'Processing-core Authenticode regression test exceeded its 60-second watchdog'
        }
        if ($testProcess.ExitCode -ne 0) {
            throw "Processing-core Authenticode regression test failed with exit code $($testProcess.ExitCode)"
        }
    } finally {
        $testProcess.Dispose()
    }
} finally {
    if ($rootStore) {
        if ($certificate) {
            $rootStore.Remove($certificate)
        }
        $rootStore.Close()
        $rootStore.Dispose()
    }
    if ($certificate) {
        Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($certificate.Thumbprint)" `
            -Force -ErrorAction SilentlyContinue
        $certificate.Dispose()
    }
    Remove-Item -LiteralPath $signedPath -Force -ErrorAction SilentlyContinue
}
