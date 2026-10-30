param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$ReleaseZip = '',
    [string]$PublicKey = '',
    [string]$WorkDir = ''
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($Root)
$ciDir = Join-Path $root 'ci'
if ([string]::IsNullOrWhiteSpace($WorkDir)) {
    $WorkDir = Join-Path $env:TEMP ('olho_release_verify_' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

function Fail([string]$message) {
    Write-Host "FAIL: $message"
    exit 1
}

if ([string]::IsNullOrWhiteSpace($ReleaseZip)) {
    $latest = Get-ChildItem (Join-Path $root 'releases') -Recurse -Filter 'OlhoDeDeus_release_*.zip' |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { Fail 'nenhum release encontrado' }
    $ReleaseZip = $latest.FullName
}
$ReleaseZip = [IO.Path]::GetFullPath($ReleaseZip)
if (-not (Test-Path $ReleaseZip)) { Fail "zip ausente: $ReleaseZip" }

$name = Split-Path $ReleaseZip -Leaf
$shaFile = $ReleaseZip + '.sha256'
$sigFile = $ReleaseZip + '.sig'
if (-not (Test-Path $shaFile)) { Fail "sha256 ausente: $shaFile" }
if (-not (Test-Path $sigFile)) { Fail "assinatura ausente: $sigFile" }

$expected = ((Get-Content $shaFile -Raw).Trim() -split '\s+')[0].ToLower()
$actual = (Get-FileHash $ReleaseZip -Algorithm SHA256).Hash.ToLower()
if ($expected -ne $actual) { Fail "sha256 do zip nao confere" }
Write-Host "PASS zip sha256: $actual"

if ([string]::IsNullOrWhiteSpace($PublicKey)) {
    $PublicKey = Join-Path $env:TEMP 'release_key\release.pub'
}
if (-not (Test-Path $PublicKey)) { Fail "chave publica ausente: $PublicKey" }
$deep = Join-Path $root 'OlhoDeDeus_deep.exe'
if (-not (Test-Path $deep)) { Fail "binario ausente: $deep" }
& $deep --verify-manifest $ReleaseZip --key $PublicKey --sig $sigFile
if ($LASTEXITCODE -ne 0) { Fail 'assinatura do manifesto rejeitada' }
Write-Host 'PASS assinatura ECDSA'

$extract = Join-Path $WorkDir 'extract'
if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
Expand-Archive -Path $ReleaseZip -DestinationPath $extract

$sumFile = Join-Path $extract 'SHA256SUMS.txt'
if (-not (Test-Path $sumFile)) { Fail 'SHA256SUMS.txt ausente no pacote' }
$sumErrors = 0
foreach ($line in Get-Content $sumFile) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $parts = $line -split '\s+', 2
    if ($parts.Count -ne 2) { $sumErrors++; continue }
    $file = Join-Path $extract ($parts[1].Replace('/', '\'))
    if (-not (Test-Path $file)) { Write-Host "missing: $($parts[1])"; $sumErrors++; continue }
    $hash = (Get-FileHash $file -Algorithm SHA256).Hash.ToLower()
    if ($hash -ne $parts[0].ToLower()) { Write-Host "hash mismatch: $($parts[1])"; $sumErrors++ }
}
if ($sumErrors -gt 0) { Fail "SHA256SUMS com $sumErrors erro(s)" }
Write-Host 'PASS SHA256SUMS do pacote'

$forbidden = Get-ChildItem $extract -Recurse -File |
    Where-Object { $_.Extension -in @('.pfx', '.p12', '.pem', '.key', '.priv') -or $_.Name -match 'release\.(priv|key)$' }
if ($forbidden) {
    $forbidden | ForEach-Object { Write-Host "forbidden: $($_.FullName)" }
    Fail 'material de chave encontrado no pacote'
}
Write-Host 'PASS nenhum material de chave no pacote'

$manifestPath = Join-Path $extract 'release_manifest.json'
if (-not (Test-Path $manifestPath)) { Fail 'release_manifest.json ausente' }
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
if (-not $manifest.files -or $manifest.files.Count -lt 1) { Fail 'manifesto vazio' }
Write-Host "PASS manifesto: $($manifest.files.Count) arquivos"

Write-Host "release verify: PASS"
Write-Host "zip: $ReleaseZip"
Write-Host "sha256: $actual"
exit 0
