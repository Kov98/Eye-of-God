$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$gpp = $env:OLHO_GXX
if (-not $gpp) { $gpp = 'C:\msys64\ucrt64\bin\g++.exe' }
if (-not (Test-Path $gpp)) { $c = Get-Command g++ -ErrorAction SilentlyContinue; if ($c) { $gpp = $c.Source } }
if (-not $gpp -or -not (Test-Path $gpp)) { throw 'g++ not found' }
if (-not (Test-Path $gpp)) { throw 'g++ not found' }
$sources = @(
    'cloud/service/service_main.cpp',
    'cloud/service/api.cpp',
    'cloud/service/job_queue.cpp',
    'cloud/enterprise/secret_store.cpp',
    'cloud/service/rate_limiter.cpp',
    'cloud/core/cloud_core.cpp',
    'cloud/core/oidc.cpp',
    'cloud/core/winhttp_transport.cpp',
    'cloud/core/manifest_verify.cpp',
    'cloud/core/rbac.cpp',
    'cloud/enterprise/identity.cpp',
    'cloud/enterprise/session.cpp',
    'cloud/enterprise/audit.cpp'
) | ForEach-Object { Join-Path $root $_ }
$out = Join-Path $root 'cloud_service.exe'
& $gpp -std=c++17 -O2 -fms-extensions -DCPPHTTPLIB_OPENSSL_SUPPORT '-Wl,--no-insert-timestamp' -I $root -I (Join-Path $root 'third_party') `
    -o $out @sources -lssl -lcrypto -lws2_32 -lcrypt32 -lbcrypt -lwinhttp -luser32 -ladvapi32 -pthread -static
if ($LASTEXITCODE -ne 0) { throw "service build failed: $LASTEXITCODE" }
Write-Host ("built {0} ({1:n0} bytes)" -f (Split-Path $out -Leaf), (Get-Item $out).Length)
