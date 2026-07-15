# Organizational signing trust anchors

`kpt-mib-studio-root-ca.cer` is the **public** root certificate of the KPT
organizational Authenticode identity (`O=KPT, CN=KPT MIB Studio Root CA`,
SHA-256 fingerprint
`E0CA1C999D4701A3665793911375DC77D2076A7CBC0EBA076C235119E8D2F06A` without
separators). It contains no private material; the code-signing private key
exists only in the GitHub `Production` environment secret
`WINDOWS_SIGNING_CERTIFICATE_BASE64` and in the operator's offline backup.

The trust root for processing-core activation remains the compiled DER-SPKI
pin (`MIB_PROCESSING_CORE_SIGNER_SPKI_SHA256`); this certificate only lets
Windows chain-validate the Authenticode signature.

## Where it is used

- **Release CI** (`python-wheel.yml`, `sign-native-plugin`): imported into the
  ephemeral runner's `LocalMachine\Root` store via the .NET `X509Store` API so
  `Get-AuthenticodeSignature` can report `Valid` for the internally signed
  DLL before the SPKI pin comparison. The runner is destroyed after the job;
  no persistent trust is granted. (`certutil` is deliberately avoided — it
  hangs on hosted images; see the 2026-07-14 diagnostics on issue #239.)
- **Workstations running MIB Studio**: install once, machine-wide, from an
  elevated prompt:

  ```powershell
  certutil -addstore Root deploy\signing\kpt-mib-studio-root-ca.cer
  ```

  Without this, WinVerifyTrust rejects internally signed processing cores
  (fail closed), which is the intended behavior on unmanaged machines.

## Rotation

Rotating the leaf under the same root only requires updating the repository
SPKI variable and re-provisioning the PFX secret. Rotating the **root**
additionally requires replacing this file and re-installing it across the
workstation fleet.
