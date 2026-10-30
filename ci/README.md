# CI / release gates

## Local

```powershell
.\ci\build_and_test.ps1
```

Runs, in order, and stops reporting on failure:

1. cloud core build
2. cloud core tests
3. DB import tests
4. queue lease/fencing tests
5. worker integration with mock Graph
6. desktop build
7. service build
8. service TLS
9. worker build
10. live service+worker pause/resume/cancel
11. worker crash recovery (lease expirado)

Skip the integration gate only for a deliberate fast loop:

```powershell
.\ci\build_and_test.ps1 -SkipIntegration
```

## Signed package

```powershell
.\ci\build_and_test.ps1 `
  -Package `
  -PrivateKey C:\secure\release.priv `
  -PublicKey C:\secure\release.pub
```

The package gate runs `build_release.ps1` and then `ci\verify_release.ps1`,
which checks:

- ZIP SHA-256 against the `.sha256` file;
- ECDSA signature of the release manifest;
- every file in `SHA256SUMS.txt`;
- no key material (`.pem`, `.pfx`, `.key`, `.priv`) in the package;
- non-empty release manifest.

## GitHub Actions

- `.github/workflows/ci.yml` runs the unsigned quality gates on pushes and PRs.
- Release signing is intentionally not enabled until a real HSM/TPM-backed
  key and Authenticode certificate exist. Do not put the production private
  key in repository secrets.

## Still external

- Authenticode signing;
- HSM/TPM key custody;
- Microsoft 365 tenant and app registration;
- OIDC IdP.

## Authenticode

Sign a staging copy (never the release ZIP after hashing):

```powershell
.\ci\sign_authenticode.ps1 `
  -Files .\dist_full\OlhoDeDeus_gui.exe, .\dist_full\cloud_service.exe `
  -PfxPath C:\secure\codesign.pfx `
  -PfxPassword (Read-Host -AsSecureString)
```

Verify:

```powershell
.\ci\verify_authenticode.ps1 -Directory .\dist_full -ExpectedThumbprint <thumb>
```

For a test certificate use `-AllowUntrusted`; production requires a
trusted certificate and `Status = Valid`. Authenticode signing must run
before `build_release.ps1`, because signing changes every binary hash.