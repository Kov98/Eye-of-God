param(
    [string]$PrivateKey = '',
    [string]$PublicKey = '',
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root 'dist_full'
$deep = Join-Path $root 'OlhoDeDeus_deep.exe'
if (-not (Test-Path $dist)) { throw "missing $dist" }
if (-not (Test-Path $deep)) { throw "missing $deep" }

if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $root ('releases\' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Rebuild the release file list. SHA256SUMS is not hashed into itself.
$files = Get-ChildItem $dist -Recurse -File | Where-Object {
    $_.Name -notin @('SHA256SUMS.txt', 'release_manifest.json') -and
    $_.Extension -notin @('.zip', '.sig')
} | Sort-Object FullName

$sumLines = New-Object System.Collections.Generic.List[string]
$manifestFiles = New-Object System.Collections.Generic.List[object]
foreach ($file in $files) {
    $relative = $file.FullName.Substring($dist.Length + 1).Replace('\', '/')
    $hash = (Get-FileHash $file.FullName -Algorithm SHA256).Hash.ToLower()
    $sumLines.Add("$hash  $relative")
    $manifestFiles.Add([ordered]@{
        path = $relative
        size = $file.Length
        sha256 = $hash
    })
}
$sumLines | Set-Content (Join-Path $dist 'SHA256SUMS.txt') -Encoding ascii

$manifest = [ordered]@{
    product = 'Olho de Deus / Deep Engine'
    channel = 'cloud-enterprise'
    built_at_utc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    files = $manifestFiles
} | ConvertTo-Json -Depth 6
$manifest | Set-Content (Join-Path $dist 'release_manifest.json') -Encoding utf8

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$zipName = "OlhoDeDeus_release_$stamp.zip"
$zipPath = Join-Path $OutDir $zipName
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path (Join-Path $dist '*') -DestinationPath $zipPath -CompressionLevel Optimal

$zipHash = (Get-FileHash $zipPath -Algorithm SHA256).Hash.ToLower()
$hashLine = "$zipHash  $zipName"
$hashLine | Set-Content ($zipPath + '.sha256') -Encoding ascii

if (-not [string]::IsNullOrWhiteSpace($PrivateKey)) {
    $private = [IO.Path]::GetFullPath($PrivateKey)
    if (-not (Test-Path $private)) { throw "missing private key $private" }
    $sig = $zipPath + '.sig'
    & $deep --sign-manifest $zipPath --key $private --sig $sig
    if ($LASTEXITCODE -ne 0) { throw "release signing failed: $LASTEXITCODE" }
    if ([string]::IsNullOrWhiteSpace($PublicKey)) {
        $PublicKey = [IO.Path]::ChangeExtension($private, '.pub')
    }
    $public = [IO.Path]::GetFullPath($PublicKey)
    if (-not (Test-Path $public)) { throw "missing public key $public" }
    & $deep --verify-manifest $zipPath --key $public --sig $sig | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "release signature verification failed" }
}

Copy-Item (Join-Path $dist 'SHA256SUMS.txt') $OutDir -Force
Copy-Item (Join-Path $dist 'release_manifest.json') $OutDir -Force
Write-Host "release: $zipPath"
Write-Host "sha256: $zipHash"
if (Test-Path ($zipPath + '.sig')) { Write-Host "signature: $($zipPath + '.sig')" }
Write-Host "size: $((Get-Item $zipPath).Length) bytes"
