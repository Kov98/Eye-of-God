param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [switch]$SkipIntegration,
    [string]$PrivateKey = '',
    [string]$PublicKey = '',
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($Root)
$script:failures = New-Object System.Collections.Generic.List[string]
$script:gates = New-Object System.Collections.Generic.List[object]

function Gate([string]$name, [scriptblock]$body) {
    Write-Host ""
    Write-Host "== $name =="
    $start = Get-Date
    try {
        & $body
        if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
            throw "exit code $LASTEXITCODE"
        }
        $elapsed = [int]((Get-Date) - $start).TotalSeconds
        $script:gates.Add([pscustomobject]@{ gate = $name; status = 'PASS'; seconds = $elapsed })
        Write-Host "PASS $name ($elapsed s)"
    } catch {
        $elapsed = [int]((Get-Date) - $start).TotalSeconds
        $script:gates.Add([pscustomobject]@{ gate = $name; status = 'FAIL'; seconds = $elapsed })
        $script:failures.Add("$name : $_")
        Write-Host "FAIL $name : $_"
    }
}

function RunPs([string]$script, [string[]]$arguments = @()) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $script @arguments
    if ($LASTEXITCODE -ne 0) { throw "$script failed with $LASTEXITCODE" }
}

function RunExe([string]$relative) {
    $exe = Join-Path $root $relative
    if (-not (Test-Path $exe)) { throw "missing $exe" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "$relative failed with $LASTEXITCODE" }
}

if ($env:OLHO_GXX) { $env:PATH = (Split-Path $env:OLHO_GXX -Parent) + ';' + $env:PATH }

Gate 'cloud core build' { RunPs (Join-Path $root 'build_cloud.ps1') }
Gate 'cloud core tests' { RunExe 'cloud_core_test.exe' }
Gate 'db import tests' { RunExe 'cloud_db_import_test.exe' }
Gate 'queue lease tests' { RunExe 'job_queue_lease_test.exe' }
Gate 'desktop build' { RunPs (Join-Path $root 'build_deep.ps1') }
Gate 'service build' { RunPs (Join-Path $root 'build_service.ps1') }
if (-not $SkipIntegration) {
    Gate 'service TLS' {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'cloud\tests\tls_service_test.ps1')
        if ($LASTEXITCODE -ne 0) { throw "TLS service test failed with $LASTEXITCODE" }
    }
}
Gate 'worker build' { RunPs (Join-Path $root 'build_worker.ps1') }
if (-not $SkipIntegration) {
    Gate 'worker integration' {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'cloud\tests\worker_integration.ps1')
        if ($LASTEXITCODE -ne 0) { throw "worker integration failed with $LASTEXITCODE" }
    }
}
if (-not $SkipIntegration) {
    Gate 'live pause/resume/cancel' {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'cloud\tests\live_state_test.ps1')
        if ($LASTEXITCODE -ne 0) { throw "live state test failed with $LASTEXITCODE" }
    }
    Gate 'worker crash recovery' {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'cloud\tests\worker_crash_recovery_test.ps1')
        if ($LASTEXITCODE -ne 0) { throw "worker crash recovery test failed with $LASTEXITCODE" }
    }
}

if ($Package) {
    Gate 'signed release' {
        if ([string]::IsNullOrWhiteSpace($PrivateKey) -or -not (Test-Path $PrivateKey)) {
            throw 'release package requires a valid -PrivateKey'
        }
        if ([string]::IsNullOrWhiteSpace($PublicKey)) {
            $PublicKey = [IO.Path]::ChangeExtension($PrivateKey, '.pub')
        }
        if (-not (Test-Path $PublicKey)) { throw "missing public key $PublicKey" }
        RunPs (Join-Path $root 'build_release.ps1') @('-PrivateKey', $PrivateKey, '-PublicKey', $PublicKey)
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'ci\verify_release.ps1') -PublicKey $PublicKey
        if ($LASTEXITCODE -ne 0) { throw "release verification failed" }
    }
}

Write-Host ""
$script:gates | Format-Table -AutoSize
if ($script:failures.Count -gt 0) {
    Write-Host "CI FAIL"
    $script:failures | ForEach-Object { Write-Host " - $_" }
    exit 1
}
Write-Host 'CI PASS'
exit 0
