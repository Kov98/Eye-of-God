param(
    [Parameter(Mandatory = $true)][string[]]$Files,
    [Parameter(Mandatory = $true)][string]$PfxPath,
    [object]$PfxPassword = '',
    [string]$TimestampUrl = 'http://timestamp.digicert.com',
    [string]$Signtool = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Signtool)) {
    $candidate = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1
    if (-not $candidate) { throw 'signtool.exe x64 not found' }
    $Signtool = $candidate.FullName
}
$PfxPath = [IO.Path]::GetFullPath($PfxPath)
if (-not (Test-Path $PfxPath)) { throw "PFX ausente: $PfxPath" }
if (-not (Test-Path $Signtool)) { throw "signtool ausente: $Signtool" }
if ($PfxPassword -is [System.Security.SecureString]) {
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($PfxPassword)
    try { $PfxPassword = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}

$resolved = @()
foreach ($file in $Files) {
    $full = [IO.Path]::GetFullPath($file)
    if (-not (Test-Path $full)) { throw "arquivo ausente: $full" }
    $resolved += $full
}

$signArgs = @('sign', '/fd', 'SHA256', '/f', $PfxPath, '/p', $PfxPassword, '/td', 'SHA256')
if (-not [string]::IsNullOrWhiteSpace($TimestampUrl)) {
    $signArgs += @('/tr', $TimestampUrl)
}
$signArgs += $resolved

& $Signtool @signArgs
if ($LASTEXITCODE -ne 0) { throw "signtool sign failed: $LASTEXITCODE" }

foreach ($file in $resolved) {
    $sig = Get-AuthenticodeSignature -FilePath $file
    if (-not $sig.SignerCertificate) { throw "sem certificado de assinatura: $file" }
    Write-Host "signed: $file"
    Write-Host "  subject: $($sig.SignerCertificate.Subject)"
    Write-Host "  thumbprint: $($sig.SignerCertificate.Thumbprint)"
    Write-Host "  status: $($sig.Status)"
}
Write-Host 'authenticode sign: PASS'
