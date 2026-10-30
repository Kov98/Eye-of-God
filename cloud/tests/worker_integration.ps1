# End-to-end worker integration with a local mock Graph server and DPAPI refs.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$gpp = $env:OLHO_GXX
if (-not $gpp) { $gpp = 'C:\msys64\ucrt64\bin\g++.exe' }
if (-not (Test-Path $gpp)) { $gpp = (Get-Command g++ -ErrorAction SilentlyContinue).Source }
$python = $env:OLHO_PYTHON
if (-not $python) { $python = 'C:\Users\pedri\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe' }
if (-not (Test-Path $python)) { $python = (Get-Command python -ErrorAction SilentlyContinue).Source }
if (-not (Test-Path $gpp)) { throw 'g++ not found' }
if (-not (Test-Path $python)) { throw 'python not found' }
$temp = Join-Path $env:TEMP 'olho_worker_integration'
Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path (Join-Path $temp 'case'), (Join-Path $temp 'queue') | Out-Null
$setup = Join-Path $temp 'setup.exe'
& $gpp -std=c++17 -O2 -fms-extensions -I $root -I (Join-Path $root 'third_party') `
    -o $setup (Join-Path $root 'cloud\tests\worker_integration_setup.cpp') `
    (Join-Path $root 'cloud\service\job_queue.cpp') `
    (Join-Path $root 'cloud\enterprise\secret_store.cpp') `
    (Join-Path $root 'cloud\core\dpapi_token_vault.cpp') `
    (Join-Path $root 'cloud\core\cloud_core.cpp') `
    -lcrypt32 -lbcrypt -ladvapi32 -lws2_32
if ($LASTEXITCODE -ne 0) { throw 'setup compile failed' }
& $setup (Join-Path $temp 'case') (Join-Path $temp 'queue') 'integration/vault'
if ($LASTEXITCODE -ne 0) { throw 'setup failed' }
$server = Start-Process -FilePath $python `
    -ArgumentList @((Join-Path $root 'cloud\tests\mock_graph.py'), '18999') `
    -PassThru -WindowStyle Hidden
try {
    Start-Sleep -Seconds 1
    & (Join-Path $root 'cloud_worker.exe') --case (Join-Path $temp 'case') `
        --queue (Join-Path $temp 'queue') --base-url 'http://127.0.0.1:18999/v1.0' `
        --mock-http --lease-seconds 30 --worker-id integration
    if ($LASTEXITCODE -ne 0) { throw "worker failed: $LASTEXITCODE" }
} finally {
    Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
}
$manifest = Get-ChildItem (Join-Path $temp 'case\cloud\m365') -Recurse -Filter manifest.json |
    Select-Object -First 1
if (-not $manifest) { throw 'manifest missing' }
$manifestJson = Get-Content $manifest.FullName -Raw | ConvertFrom-Json
if ($manifestJson.status -ne 'complete') { throw "manifest status: $($manifestJson.status)" }
$job = Get-Content (Join-Path $temp 'queue\integration-job.json') -Raw | ConvertFrom-Json
if ($job.status -ne 'completed') { throw "job status: $($job.status)" }
if (-not $job.vault_ref) { throw 'vault_ref missing' }
if ($job.vault_key_protected) { throw 'vault_key_protected should be empty' }
Write-Host 'worker integration: PASS'
