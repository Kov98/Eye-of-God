# Live crash recovery: kill a worker holding a lease, then let a new worker recover it.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$gpp = $env:OLHO_GXX
if (-not $gpp) { $gpp = 'C:\msys64\ucrt64\bin\g++.exe' }
if (-not (Test-Path $gpp)) { $gpp = (Get-Command g++ -ErrorAction SilentlyContinue).Source }
if (-not (Test-Path $gpp)) { throw 'g++ not found' }
$python = $env:OLHO_PYTHON
if (-not $python) { $python = 'C:\Users\pedri\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe' }
if (-not (Test-Path $python)) { $python = (Get-Command python -ErrorAction SilentlyContinue).Source }
if (-not (Test-Path $python)) { throw 'python not found' }

$temp = Join-Path $env:TEMP 'olho_crash_recovery'
Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
$case = Join-Path $temp 'case'
$queueDir = Join-Path $temp 'queue'
New-Item -ItemType Directory -Force -Path $case, $queueDir | Out-Null
$setup = Join-Path $temp 'setup.exe'
& $gpp -std=c++17 -O2 -fms-extensions -I $root -I (Join-Path $root 'third_party') `
    -o $setup (Join-Path $root 'cloud\tests\worker_integration_setup.cpp') `
    (Join-Path $root 'cloud\service\job_queue.cpp') `
    (Join-Path $root 'cloud\enterprise\secret_store.cpp') `
    (Join-Path $root 'cloud\core\dpapi_token_vault.cpp') `
    (Join-Path $root 'cloud\core\cloud_core.cpp') `
    -lcrypt32 -lbcrypt -ladvapi32 -lws2_32
if ($LASTEXITCODE -ne 0) { throw 'setup compile failed' }
& $setup $case $queueDir 'recovery/vault'
if ($LASTEXITCODE -ne 0) { throw 'setup failed' }

$port = 18997
$mock = Start-Process -FilePath $python `
    -ArgumentList @((Join-Path $root 'cloud\tests\mock_graph.py'), $port, '700') `
    -PassThru -WindowStyle Hidden
$worker1 = $null
$worker2 = $null
try {
    Start-Sleep -Seconds 1
    $args1 = @('--case', $case, '--queue', $queueDir,
        '--base-url', "http://127.0.0.1:$port/v1.0", '--mock-http',
        '--watch', '--lease-seconds', '30', '--worker-id', 'crash-a')
    $worker1 = Start-Process -FilePath (Join-Path $root 'cloud_worker.exe') `
        -ArgumentList $args1 -PassThru -WindowStyle Hidden

    $jobPath = Join-Path $queueDir 'integration-job.json'
    $deadline = (Get-Date).AddSeconds(20)
    $status = ''
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $jobPath) {
            $status = (Get-Content $jobPath -Raw | ConvertFrom-Json).status
            if ($status -eq 'running') { break }
        }
        Start-Sleep -Milliseconds 200
    }
    if ($status -ne 'running') { throw "worker did not claim job; status=$status" }
    Write-Host 'worker A running, killing it'
    Stop-Process -Id $worker1.Id -Force -ErrorAction SilentlyContinue
    $worker1 = $null
    $lease = (Get-Content $jobPath -Raw | ConvertFrom-Json).lease_until_epoch
    $remain = [Math]::Max(1, $lease - [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() + 2)
    Write-Host "waiting $remain s for lease expiry"
    Start-Sleep -Seconds $remain

    $args2 = @('--case', $case, '--queue', $queueDir,
        '--base-url', "http://127.0.0.1:$port/v1.0", '--mock-http',
        '--watch', '--lease-seconds', '30', '--worker-id', 'crash-b')
    $worker2 = Start-Process -FilePath (Join-Path $root 'cloud_worker.exe') `
        -ArgumentList $args2 -PassThru -WindowStyle Hidden

    $deadline = (Get-Date).AddSeconds(40)
    $job = $null
    while ((Get-Date) -lt $deadline) {
        $job = Get-Content $jobPath -Raw | ConvertFrom-Json
        if ($job.status -eq 'completed') { break }
        if ($job.status -eq 'failed') { throw "job failed after recovery: $($job.error)" }
        Start-Sleep -Milliseconds 250
    }
    if ($job.status -ne 'completed') { throw "recovery did not complete; status=$($job.status)" }
    if ($job.attempts -lt 2) { throw "expected attempts >= 2, got $($job.attempts)" }
    $manifest = Get-ChildItem (Join-Path $case 'cloud\m365') -Recurse -Filter manifest.json |
        Select-Object -First 1
    if (-not $manifest) { throw 'manifest missing' }
    $manifestJson = Get-Content $manifest.FullName -Raw | ConvertFrom-Json
    if ($manifestJson.status -ne 'complete') { throw "manifest status: $($manifestJson.status)" }
    Write-Host "recovery: status=$($job.status) attempts=$($job.attempts)"
    Write-Host 'worker crash recovery: PASS'
} finally {
    if ($worker1) { Stop-Process -Id $worker1.Id -Force -ErrorAction SilentlyContinue }
    if ($worker2) { Stop-Process -Id $worker2.Id -Force -ErrorAction SilentlyContinue }
    if ($mock) { Stop-Process -Id $mock.Id -Force -ErrorAction SilentlyContinue }
}
