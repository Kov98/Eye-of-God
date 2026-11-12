# Reconstroi e roda toda a verificacao do modulo iOS (task-1).
# Uso: powershell -ExecutionPolicy Bypass -File .oracle_ios\run_all.ps1

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

$gxx = 'C:\msys64\ucrt64\bin\g++.exe'
if (-not (Test-Path $gxx)) { throw "compilador nao encontrado: $gxx" }

$build = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$log = Join-Path $PSScriptRoot 'test_run.log'
$harness = Join-Path $build 'test_ios.exe'

$common = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-I', '.', '-I', 'third_party/sqlite')

Write-Output '### 1/5 auto-teste do oraculo PowerShell/.NET (RFC 3394, NIST, PBKDF2)'
$selfTestPath = Join-Path $PSScriptRoot 'selftest.json'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'gen_crypto.ps1') -SelfTest -Out $selfTestPath
$selftest = Get-Content -Raw $selfTestPath | ConvertFrom-Json
Write-Output ("    oraculo: {0} de {1} vetores externos" -f $selftest.passed, $selftest.total)
if ($selftest.passed -ne $selftest.total) { throw 'oraculo reprovado nos vetores externos' }

Write-Output '### 2/5 geracao da fixture com o oraculo independente'
$fixtureOutput = & python .oracle_ios\gen_fixture.py
if ($LASTEXITCODE -ne 0) { throw 'gen_fixture.py falhou' }
$fixtureOutput | ForEach-Object { Write-Output ("    " + $_) }

Write-Output '### 3/5 compilacao limpa das unidades do modulo'
$warnings = @()
foreach ($unit in 'ios_keybag.cpp', 'ios_backup.cpp') {
    $objectPath = Join-Path $build ($unit -replace '\.cpp$', '.o')
    $out = & $gxx @common -c $unit -o $objectPath 2>&1
    if ($LASTEXITCODE -ne 0) { $out | ForEach-Object { Write-Output $_ }; throw "falha compilando $unit" }
    $warnings += ($out | Where-Object { $_ -match 'warning:' })
}
if ($warnings.Count -gt 0) {
    $warnings | ForEach-Object { Write-Output ("    " + $_) }
    throw 'compilacao com warnings'
}
Write-Output '    0 warnings com -Wall -Wextra'

Write-Output '### 4/5 link do harness'
$sources = @((Join-Path $PSScriptRoot 'test_main.cpp'), 'ios_keybag.cpp', 'ios_backup.cpp', 'third_party/sqlite/sqlite3.o')
$link = & $gxx @common @sources -o $harness -lbcrypt 2>&1
if ($LASTEXITCODE -ne 0) { $link | ForEach-Object { Write-Output $_ }; throw 'falha no link' }
$linkWarnings = ($link | Where-Object { $_ -match 'warning:' })
if ($linkWarnings) { $linkWarnings | ForEach-Object { Write-Output ('    ' + $_) }; throw 'link com warnings' }
Write-Output '    0 warnings'

Write-Output '### 5/5 execucao do harness'
& $harness $PSScriptRoot 2>&1 | Tee-Object -FilePath $log
$code = $LASTEXITCODE
Write-Output ""
Write-Output ("log completo em {0}" -f $log)
exit $code
