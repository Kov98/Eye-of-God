# run.ps1 — compila e roda toda a verificacao do formatguard contra o C++ do produto.
#
#   1. cargo build --release            (cdylib + staticlib)
#   2. link do demo_main.exe com a staticlib  -> prova de ida (header incluivel + link)
#   3. link do diff_main.exe com ios_backup.cpp + ios_keybag.cpp + sqlite3.o + staticlib
#   4. teste diferencial no corpus legitimo (.oracle_ios + plists gerados)
#   5. teste diferencial nas 4000 mutacoes, com retomada apos crash do C++
#
# Uso: powershell -ExecutionPolicy Bypass -File .oracle_rust\run.ps1
$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

$gxx = 'C:\msys64\ucrt64\bin\g++.exe'
if (-not (Test-Path $gxx)) { throw "compilador nao encontrado: $gxx" }
$env:PATH = 'C:\Users\pedri\.cargo\bin;' + $env:PATH

$out = Join-Path $PSScriptRoot 'results'
New-Item -ItemType Directory -Force -Path $out | Out-Null

$sysLibs = @(
    '-lbcrypt', '-lntdll', '-luserenv', '-lws2_32', '-ladvapi32', '-lole32', '-loleaut32',
    '-luuid', '-lshlwapi', '-luser32', '-lgdi32', '-lcomdlg32', '-lcomctl32', '-lwinspool',
    '-lmsimg32', '-lwsock32', '-lsetupapi', '-lversion', '-lshell32', '-lshcore',
    '-lcfgmgr32', '-lsynchronization', '-lwinhttp'
)
$includes = @('-I', '.', '-I', 'third_party/sqlite', '-I', 'rust/formatguard/include')

Write-Output '### 1/5 cargo build --release'
Push-Location 'rust\formatguard'
$buildLog = & cmd /c "cargo build --release 2>&1"
$cargoExit = $LASTEXITCODE
Pop-Location
$buildLog | Out-File -FilePath (Join-Path $out 'cargo_build.txt') -Encoding utf8
Write-Output ("    exit={0}, linhas de log={1}" -f $cargoExit, $buildLog.Count)
if ($cargoExit -ne 0) { $buildLog | ForEach-Object { Write-Output $_ }; throw 'cargo build falhou' }

$lib = Join-Path $repo 'rust\formatguard\target\release\libformatguard.a'
if (-not (Test-Path $lib)) { throw "staticlib nao encontrada: $lib" }
Write-Output ("    staticlib={0} bytes" -f (Get-Item $lib).Length)

Write-Output '### 2/5 link do demo (header + staticlib + g++)'
$demoExe = Join-Path $out 'demo.exe'
$demoLog = & $gxx -std=c++17 -O2 -Wall -Wextra @includes (Join-Path $PSScriptRoot 'demo_main.cpp') $lib @sysLibs -o $demoExe 2>&1
$demoExit = $LASTEXITCODE
$demoLog | Out-File -FilePath (Join-Path $out 'demo_build.txt') -Encoding utf8
$demoSize = 0
if (Test-Path $demoExe) { $demoSize = (Get-Item $demoExe).Length }
Write-Output ("    exit={0} exe={1} bytes" -f $demoExit, $demoSize)
if ($demoExit -ne 0) { $demoLog | ForEach-Object { Write-Output $_ }; throw 'link do demo falhou' }

Write-Output '### 3/5 link do harness diferencial (C++ do produto + staticlib Rust)'
$diffExe = Join-Path $out 'diff.exe'
$diffLog = & $gxx -std=c++17 -O2 @includes (Join-Path $PSScriptRoot 'diff_main.cpp') 'ios_backup.cpp' 'ios_keybag.cpp' 'third_party/sqlite/sqlite3.o' $lib @sysLibs -o $diffExe 2>&1
$diffExit = $LASTEXITCODE
$diffLog | Out-File -FilePath (Join-Path $out 'diff_build.txt') -Encoding utf8
$diffSize = 0
if (Test-Path $diffExe) { $diffSize = (Get-Item $diffExe).Length }
Write-Output ("    exit={0} exe={1} bytes" -f $diffExit, $diffSize)
if ($diffExit -ne 0) { $diffLog | ForEach-Object { Write-Output $_ }; throw 'link do harness diferencial falhou' }

# --------------------------------------------------------------- corpus legitimo
Write-Output '### 4/5 teste diferencial no corpus legitimo'
$legitList = Join-Path $out 'legit_files.txt'
$legit = New-Object System.Collections.Generic.List[string]
Get-ChildItem -Recurse -File (Join-Path $repo '.oracle_ios\fixture') |
    Where-Object { $_.Length -le 1MB } |
    ForEach-Object { $legit.Add($_.FullName) }
Get-ChildItem -File (Join-Path $repo 'rust\formatguard\tests\corpus\gen') -Filter *.plist |
    ForEach-Object { $legit.Add($_.FullName) }
$legit | Set-Content -Path $legitList -Encoding utf8
Write-Output ("    arquivos no corpus legitimo: {0}" -f $legit.Count)
$legitOut = & $diffExe --list $legitList --verbose 2>&1
$legitExit = $LASTEXITCODE
$legitOut | Out-File -FilePath (Join-Path $out 'differential_legit.txt') -Encoding utf8
$legitOut | Where-Object { $_ -match '^RESUMO|^DIFF|^DIVERGE|^IO' } | ForEach-Object { Write-Output ("    " + $_) }
Write-Output ("    exit={0}" -f $legitExit)

# ------------------------------------------------------------------- mutacoes
Write-Output '### 5/5 teste diferencial nas mutacoes (com retomada apos crash)'
$mutList = Join-Path $out 'mutation_files.txt'
$mutations = Get-ChildItem -File (Join-Path $repo 'rust\formatguard\tests\corpus\mut') -Filter *.bin | Sort-Object Name
$mutations | ForEach-Object { $_.FullName } | Set-Content -Path $mutList -Encoding utf8
Write-Output ("    mutacoes: {0}" -f $mutations.Count)
$mutLog = Join-Path $out 'differential_mutations.txt'
Remove-Item -Force $mutLog -ErrorAction SilentlyContinue
$crashes = New-Object System.Collections.Generic.List[string]
$start = 0
$total = $mutations.Count
$guard = 0
while ($start -lt $total) {
    $guard += 1
    if ($guard -gt 400) { Write-Output '    parando: 400 retomadas'; break }
    $batch = & $diffExe --list $mutList --start $start 2>&1
    $batch | Out-File -FilePath $mutLog -Append -Encoding utf8
    $results = @($batch | Where-Object { $_ -match '^(OK|OK-REJ|DIFF-JSON|DIVERGE|IO)\t' })
    $summary = @($batch | Where-Object { $_ -match '^RESUMO\t' })
    if ($summary.Count -gt 0) { break }
    $crashed = $start + $results.Count
    $name = if ($crashed -lt $mutations.Count) { $mutations[$crashed].FullName } else { '?' }
    $crashes.Add(("{0}\tindice={1}" -f $name, $crashed))
    Write-Output ("    C++ abortou no indice {0}: {1}" -f $crashed, $name)
    $start = $crashed + 1
}
$crashes | Set-Content -Path (Join-Path $out 'cpp_crashes.txt') -Encoding utf8

$all = Get-Content $mutLog
$okSame = @($all | Where-Object { $_ -match '^OK\t' }).Count
$okRej = @($all | Where-Object { $_ -match '^OK-REJ\t' }).Count
$jsonDiff = @($all | Where-Object { $_ -match '^DIFF-JSON\t' })
$diverge = @($all | Where-Object { $_ -match '^DIVERGE\t' })
$summaryLine = @($all | Where-Object { $_ -match '^RESUMO\t' }) | Select-Object -Last 1
Write-Output ("    ok_mesmo_json={0} ok_recusados={1} json_divergente={2} divergencias={3} crashes_cpp={4}" -f $okSame, $okRej, $jsonDiff.Count, $diverge.Count, $crashes.Count)
if ($jsonDiff.Count -gt 0) { $jsonDiff | Select-Object -First 5 | ForEach-Object { Write-Output ("    " + $_) } }
if ($diverge.Count -gt 0) { $diverge | Select-Object -First 5 | ForEach-Object { Write-Output ("    " + $_) } }

Write-Output '### demo: JSON impresso a partir do C++'
$demoTargets = Get-ChildItem -Recurse -File (Join-Path $repo '.oracle_ios\fixture') |
    Where-Object { $_.Name -like '*.plist' -and $_.Length -le 1MB }
foreach ($target in $demoTargets) {
    $demoLog = & $demoExe $target.FullName 2>&1
    $demoLog | Out-File -FilePath (Join-Path $out ("demo_" + $target.BaseName + "_" + $target.Directory.Name + ".txt")) -Encoding utf8
    Write-Output ("    {0} ({1} bytes) -> exit={2}" -f $target.FullName, $target.Length, $LASTEXITCODE)
    $demoLog | Select-Object -First 6 | ForEach-Object { Write-Output ("      " + $_) }
}
Write-Output 'run.ps1 terminou'
