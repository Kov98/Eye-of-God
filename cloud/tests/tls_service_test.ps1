# Live HTTPS test for cloud_service.exe using a temporary self-signed certificate.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$openssl = $env:OLHO_OPENSSL
if (-not $openssl) { $openssl = 'C:\msys64\ucrt64\bin\openssl.exe' }
if (-not (Test-Path $openssl)) { $openssl = (Get-Command openssl -ErrorAction SilentlyContinue).Source }
if (-not (Test-Path $openssl)) { throw 'openssl not found' }
$curl = (Get-Command curl.exe -ErrorAction SilentlyContinue).Source
if (-not $curl) { throw 'curl.exe not found' }

$temp = Join-Path $env:TEMP 'olho_tls_service'
Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $temp | Out-Null
$cert = Join-Path $temp 'cert.pem'
$key = Join-Path $temp 'key.pem'

$oldEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days 1 `
    -keyout $key -out $cert -subj '/CN=localhost' `
    -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' 2>&1 | Out-Null
$opensslExit = $LASTEXITCODE
$ErrorActionPreference = $oldEap
if ($opensslExit -ne 0) { throw 'openssl cert generation failed' }

$port = 8769
$service = Start-Process -FilePath (Join-Path $root 'cloud_service.exe') `
    -ArgumentList @('--case', $temp, '--port', $port, '--tls-cert', $cert, '--tls-key', $key) `
    -PassThru -WindowStyle Hidden
try {
    Start-Sleep -Seconds 1
    $health = & $curl -sS --cacert $cert "https://localhost:$port/health"
    if ($LASTEXITCODE -ne 0) { throw 'HTTPS health request failed' }
    if ($health -notmatch '"status":"ok"') { throw "unexpected health: $health" }
    Write-Host "https health: $health"

    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $curl -sS -o NUL "http://127.0.0.1:$port/health" 2>&1 | Out-Null
    $plainExit = $LASTEXITCODE
    $ErrorActionPreference = $oldEap
    if ($plainExit -eq 0) { throw 'plain HTTP unexpectedly succeeded on TLS port' }
    Write-Host 'plain HTTP rejected: PASS'
    Write-Host 'tls service test: PASS'
} finally {
    if ($service) { Stop-Process -Id $service.Id -Force -ErrorAction SilentlyContinue }
    Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
}
