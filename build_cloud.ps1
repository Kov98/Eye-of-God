# Build and run the Phase 0 cloud-core unit tests.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$gpp = $env:OLHO_GXX
if (-not $gpp) { $gpp = 'C:\msys64\ucrt64\bin\g++.exe' }
if (-not (Test-Path $gpp)) { $c = Get-Command g++ -ErrorAction SilentlyContinue; if ($c) { $gpp = $c.Source } }
if (-not $gpp -or -not (Test-Path $gpp)) { throw 'g++ not found' }
$out = Join-Path $root 'cloud_core_test.exe'
$smoke = Join-Path $root 'cloud_transport_smoke.exe'
$auth = Join-Path $root 'cloud_auth.exe'
$collect = Join-Path $root 'cloud_collect.exe'
$common = @(
    (Join-Path $root 'cloud\core\cloud_core.cpp')
)
$testSources = $common + @(
    (Join-Path $root 'cloud\core\dpapi_token_vault.cpp'),
    (Join-Path $root 'cloud\core\oauth_pkce.cpp'),
    (Join-Path $root 'cloud\core\manifest_verify.cpp'),
    (Join-Path $root 'cloud\core\rbac.cpp'),
    (Join-Path $root 'cloud\core\oidc.cpp'),
    (Join-Path $root 'cloud\enterprise\identity.cpp'),
    (Join-Path $root 'cloud\enterprise\session.cpp'),
    (Join-Path $root 'cloud\enterprise\audit.cpp'),
    (Join-Path $root 'cloud\enterprise\session_vault.cpp'),
    (Join-Path $root 'cloud\enterprise\secret_store.cpp'),
    (Join-Path $root 'cloud\service\api.cpp'),
    (Join-Path $root 'cloud\service\job_queue.cpp'),
    (Join-Path $root 'cloud\service\rate_limiter.cpp'),
    (Join-Path $root 'cloud\core\oauth_callback.cpp'),
    (Join-Path $root 'cloud\core\winhttp_transport.cpp'),
    (Join-Path $root 'cloud\connectors\m365\graph_client.cpp'),
    (Join-Path $root 'cloud\connectors\m365\m365_collector.cpp'),
    (Join-Path $root 'cloud\tests\cloud_core_test.cpp')
)
& $gpp -std=c++17 -O2 -Wall -Wextra -I $root -o $out @testSources -lcrypt32 -lbcrypt -lwinhttp -lws2_32
if ($LASTEXITCODE -ne 0) { throw "cloud core build failed: $LASTEXITCODE" }
Write-Host "built $out"

$smokeSources = $common + @(
    (Join-Path $root 'cloud\core\winhttp_transport.cpp'),
    (Join-Path $root 'cloud\tests\cloud_transport_smoke.cpp')
)
& $gpp -std=c++17 -O2 -Wall -Wextra -I $root -o $smoke @smokeSources -lwinhttp -lbcrypt
if ($LASTEXITCODE -ne 0) { throw "cloud transport smoke build failed: $LASTEXITCODE" }
Write-Host "built $smoke"

$authSources = @(
    (Join-Path $root 'cloud\core\cloud_core.cpp'),
    (Join-Path $root 'cloud\core\oauth_pkce.cpp'),
    (Join-Path $root 'cloud\core\oauth_callback.cpp'),
    (Join-Path $root 'cloud\core\winhttp_transport.cpp'),
    (Join-Path $root 'cloud\core\dpapi_token_vault.cpp'),
    (Join-Path $root 'cloud\cli\auth_flow.cpp'),
    (Join-Path $root 'cloud\cli\cloud_auth.cpp')
)
& $gpp -std=c++17 -O2 -Wall -Wextra -I $root -o $auth @authSources -lwinhttp -lcrypt32 -lbcrypt -lshell32 -lws2_32 -lole32
if ($LASTEXITCODE -ne 0) { throw "cloud auth build failed: $LASTEXITCODE" }
Write-Host "built $auth"
$collectSources = @(
    (Join-Path $root 'cloud\core\cloud_core.cpp'),
    (Join-Path $root 'cloud\core\winhttp_transport.cpp'),
    (Join-Path $root 'cloud\core\dpapi_token_vault.cpp'),
    (Join-Path $root 'cloud\connectors\m365\graph_client.cpp'),
    (Join-Path $root 'cloud\connectors\m365\m365_collector.cpp'),
    (Join-Path $root 'cloud\cli\collect_flow.cpp'),
    (Join-Path $root 'cloud\cli\cloud_collect.cpp')
)
& $gpp -std=c++17 -O2 -Wall -Wextra -I $root -o $collect @collectSources -lwinhttp -lcrypt32 -lbcrypt -lws2_32
if ($LASTEXITCODE -ne 0) { throw "cloud collect build failed: $LASTEXITCODE" }
Write-Host "built $collect"
$gcc = Join-Path (Split-Path $gpp -Parent) 'gcc.exe'
$sqliteDir = Join-Path $root 'third_party\sqlite'
$sqliteObj = Join-Path $sqliteDir 'sqlite3.o'
if (-not (Test-Path $sqliteObj) -or (Get-Item (Join-Path $sqliteDir 'sqlite3.c')).LastWriteTime -gt (Get-Item $sqliteObj).LastWriteTime) {
    & $gcc -O2 -c (Join-Path $sqliteDir 'sqlite3.c') -o $sqliteObj
    if ($LASTEXITCODE -ne 0) { throw "sqlite build failed: $LASTEXITCODE" }
}
$dbTest = Join-Path $root 'cloud_db_import_test.exe'
& $gpp -std=c++17 -O2 -fms-extensions -I $root -I (Join-Path $root 'third_party') -I $sqliteDir `
    -o $dbTest (Join-Path $root 'cloud\tests\cloud_db_import_test.cpp') `
    (Join-Path $root 'cloud\db\cloud_db_import.cpp') (Join-Path $root 'case_db.cpp') `
    (Join-Path $root 'cloud\core\cloud_core.cpp') $sqliteObj -lbcrypt
if ($LASTEXITCODE -ne 0) { throw "db import test build failed: $LASTEXITCODE" }
Write-Host "built $dbTest"
$leaseTest = Join-Path $root 'job_queue_lease_test.exe'
& $gpp -std=c++17 -O2 -fms-extensions -I $root -I (Join-Path $root 'third_party') `
    -o $leaseTest (Join-Path $root 'cloud\tests\job_queue_lease_test.cpp') `
    (Join-Path $root 'cloud\service\job_queue.cpp') (Join-Path $root 'cloud\enterprise\secret_store.cpp') `
    (Join-Path $root 'cloud\core\dpapi_token_vault.cpp') (Join-Path $root 'cloud\core\cloud_core.cpp') `
    -lcrypt32 -lbcrypt -ladvapi32 -lws2_32
if ($LASTEXITCODE -ne 0) { throw "lease test build failed: $LASTEXITCODE" }
Write-Host "built $leaseTest"

& $out
if ($LASTEXITCODE -ne 0) { throw "cloud core tests failed: $LASTEXITCODE" }
Write-Host "cloud core tests: PASS"
