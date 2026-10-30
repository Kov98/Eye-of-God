# Live service+worker state test: pause, resume and cancel.
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

$temp = Join-Path $env:TEMP 'olho_live_state'
Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
$case = Join-Path $temp 'case'
$queueDir = Join-Path $case 'cloud\queue'
New-Item -ItemType Directory -Force -Path $case, $queueDir | Out-Null
$identity = Join-Path $temp 'identity.json'
$setup = Join-Path $temp 'setup.exe'
& $gpp -std=c++17 -O2 -fms-extensions -I $root -I (Join-Path $root 'third_party') `
    -o $setup (Join-Path $root 'cloud\tests\gui_e2e_setup.cpp') `
    (Join-Path $root 'cloud\enterprise\identity.cpp') `
    (Join-Path $root 'cloud\core\rbac.cpp') `
    (Join-Path $root 'cloud\core\dpapi_token_vault.cpp') `
    (Join-Path $root 'cloud\core\cloud_core.cpp') `
    -lbcrypt -lcrypt32
if ($LASTEXITCODE -ne 0) { throw 'setup compile failed' }
& $setup $identity 'state/vault'
if ($LASTEXITCODE -ne 0) { throw 'setup failed' }

$mockPort = 18998
$servicePort = 8768
$mock = Start-Process -FilePath $python `
    -ArgumentList @((Join-Path $root 'cloud\tests\mock_graph.py'), $mockPort, '700') `
    -PassThru -WindowStyle Hidden
$service = $null
$worker = $null
try {
    Start-Sleep -Seconds 1
    $service = Start-Process -FilePath (Join-Path $root 'cloud_service.exe') `
        -ArgumentList @('--case', $case, '--port', $servicePort, '--identity', $identity) `
        -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds 1
    $login = Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:$servicePort/v1/session" `
        -ContentType 'application/json' `
        -Body (@{ username = 'admin'; password = 'TestPass12345!' } | ConvertTo-Json)
    $headers = @{ Authorization = "Bearer $($login.token)" }
    $worker = Start-Process -FilePath (Join-Path $root 'cloud_worker.exe') `
        -ArgumentList @('--case', $case, '--queue', $queueDir,
            '--base-url', "http://127.0.0.1:$mockPort/v1.0", '--mock-http',
            '--watch', '--lease-seconds', '30', '--worker-id', 'state-test') `
        -PassThru -WindowStyle Hidden

    function Enqueue([string]$jobId) {
        $body = @{
            job_id = $jobId
            account = 'analyst@example.test'
            tenant_id = 'tenant'
            legal_basis = 'state-test'
            vault_key = 'state/vault'
            max_pages = 20
            download_attachments = $false
        } | ConvertTo-Json
        Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:$servicePort/v1/queue" `
            -Headers $headers -ContentType 'application/json' -Body $body | Out-Null
    }
    function JobStatus([string]$jobId) {
        $path = Join-Path $queueDir ($jobId + '.json')
        if (-not (Test-Path $path)) { return '' }
        (Get-Content $path -Raw | ConvertFrom-Json).status
    }
    function WaitFor([string]$jobId, [string[]]$wanted, [int]$seconds = 30) {
        $deadline = (Get-Date).AddSeconds($seconds)
        while ((Get-Date) -lt $deadline) {
            $status = JobStatus $jobId
            if ($wanted -contains $status) { return $status }
            Start-Sleep -Milliseconds 200
        }
        throw "timeout waiting for $($wanted -join '/') on $jobId; current=$(JobStatus $jobId)"
    }

    Enqueue 'state-pause'
    $null = WaitFor 'state-pause' @('running')
    Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:$servicePort/v1/queue/state-pause/pause" `
        -Headers $headers | Out-Null
    $paused = WaitFor 'state-pause' @('paused')
    Write-Host "pause: $paused"

    Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:$servicePort/v1/queue/state-pause/resume" `
        -Headers $headers | Out-Null
    $completed = WaitFor 'state-pause' @('completed') 40
    Write-Host "resume: $completed"

    Enqueue 'state-cancel'
    $null = WaitFor 'state-cancel' @('running')
    Invoke-RestMethod -Method Delete -Uri "http://127.0.0.1:$servicePort/v1/queue/state-cancel" `
        -Headers $headers | Out-Null
    $cancelled = WaitFor 'state-cancel' @('cancelled')
    Write-Host "cancel: $cancelled"

    Write-Host 'live state test: PASS'
} finally {
    if ($worker) { Stop-Process -Id $worker.Id -Force -ErrorAction SilentlyContinue }
    if ($service) { Stop-Process -Id $service.Id -Force -ErrorAction SilentlyContinue }
    if ($mock) { Stop-Process -Id $mock.Id -Force -ErrorAction SilentlyContinue }
}
