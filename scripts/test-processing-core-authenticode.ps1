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
$pfxPath = Join-Path $WorkingDirectory 'mib-processing-authenticode-ephemeral.pfx'
Copy-Item -LiteralPath $unsignedPath -Destination $signedPath -Force

$certificate = $null
$trustedCertificate = $null
$rsa = $null
$rootStore = $null
try {
    Write-Host 'Creating ephemeral Authenticode signer'
    $rsa = [System.Security.Cryptography.RSA]::Create(2048)
    $request = [System.Security.Cryptography.X509Certificates.CertificateRequest]::new(
        'CN=MIB Processing Core Ephemeral CI Fixture',
        $rsa,
        [System.Security.Cryptography.HashAlgorithmName]::SHA256,
        [System.Security.Cryptography.RSASignaturePadding]::Pkcs1
    )
    $enhancedKeyUsages = [System.Security.Cryptography.OidCollection]::new()
    [void]$enhancedKeyUsages.Add(
        [System.Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3')
    )
    [void]$request.CertificateExtensions.Add(
        [System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new(
            $enhancedKeyUsages,
            $true
        )
    )
    [void]$request.CertificateExtensions.Add(
        [System.Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new(
            [System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature,
            $true
        )
    )
    [void]$request.CertificateExtensions.Add(
        [System.Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new(
            $false,
            $false,
            0,
            $true
        )
    )
    $certificate = $request.CreateSelfSigned(
        [DateTimeOffset]::UtcNow.AddMinutes(-5),
        [DateTimeOffset]::UtcNow.AddDays(1)
    )
    $pfxPassword = [Guid]::NewGuid().ToString('N')
    [IO.File]::WriteAllBytes(
        $pfxPath,
        $certificate.Export(
            [System.Security.Cryptography.X509Certificates.X509ContentType]::Pfx,
            $pfxPassword
        )
    )
    $trustedCertificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
        $certificate.Export(
            [System.Security.Cryptography.X509Certificates.X509ContentType]::Cert
        )
    )

    $rootStore = [System.Security.Cryptography.X509Certificates.X509Store]::new(
        [System.Security.Cryptography.X509Certificates.StoreName]::Root,
        [System.Security.Cryptography.X509Certificates.StoreLocation]::CurrentUser
    )
    $rootStore.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
    $rootStore.Add($trustedCertificate)

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
    if ($rootStore) {
        if ($trustedCertificate) {
            $rootStore.Remove($trustedCertificate)
        }
        $rootStore.Close()
        $rootStore.Dispose()
    }
    if ($trustedCertificate) {
        $trustedCertificate.Dispose()
    }
    if ($certificate) {
        $certificate.Dispose()
    }
    if ($rsa) {
        $rsa.Dispose()
    }
    Remove-Item -LiteralPath $pfxPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $signedPath -Force -ErrorAction SilentlyContinue
}
