param(
    [Parameter(Mandatory = $true)][string]$Directory,
    [string]$ExpectedThumbprint = '',
    [switch]$AllowUntrusted
)

$ErrorActionPreference = 'Stop'
$Directory = [IO.Path]::GetFullPath($Directory)
if (-not (Test-Path $Directory)) { throw "diretorio ausente: $Directory" }

$files = Get-ChildItem $Directory -Filter '*.exe' -File
if ($files.Count -eq 0) { throw 'nenhum executavel encontrado' }

$failures = 0
foreach ($file in $files) {
    $sig = Get-AuthenticodeSignature -FilePath $file.FullName
    $ok = $false
    if ($sig.SignerCertificate) {
        if ($AllowUntrusted) {
            $ok = $true
        } elseif ($sig.Status -eq 'Valid') {
            $ok = $true
        }
        if ($ok -and -not [string]::IsNullOrWhiteSpace($ExpectedThumbprint) -and
            $sig.SignerCertificate.Thumbprint -ne $ExpectedThumbprint) {
            $ok = $false
            Write-Host "thumbprint mismatch: $($file.Name)"
        }
    }
    if (-not $ok) {
        $failures++
        Write-Host "FAIL $($file.Name) status=$($sig.Status)"
    } else {
        Write-Host "PASS $($file.Name) status=$($sig.Status) signer=$($sig.SignerCertificate.Subject)"
    }
}

if ($failures -gt 0) {
    Write-Host "authenticode verify: FAIL ($failures)"
    exit 1
}
Write-Host 'authenticode verify: PASS'
exit 0
