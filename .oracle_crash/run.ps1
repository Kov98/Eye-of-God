# Requires -Version 5.1
# Builds and runs the isolated reproduction for docs/CRASHES-E-BUGS.md #9.
#
# Native processes go through Start-Process on purpose: Windows PowerShell turns
# a native program's stderr into a terminating error record when
# $ErrorActionPreference = 'Stop', which would abort the run instead of counting
# the crashes.
$ErrorActionPreference = 'Stop'
$nl = [Environment]::NewLine

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
$build = Join-Path $here 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null

$gpp = 'C:\msys64\ucrt64\bin\g++.exe'
$pkg = 'C:\msys64\ucrt64\bin\pkg-config.exe'
$a2l = 'C:\msys64\ucrt64\bin\addr2line.exe'

$pkgOut = Join-Path $build 'pkgconfig.txt'
$pkgProc = Start-Process -FilePath $pkg -ArgumentList @('--cflags', '--libs', 'Qt6Widgets') -NoNewWindow -Wait -PassThru -RedirectStandardOutput $pkgOut
if ($pkgProc.ExitCode -ne 0) { throw 'pkg-config failed for Qt6Widgets' }
$qtFlags = ((Get-Content -Raw $pkgOut) -split '\s+') | Where-Object { $_ -ne '' }

# -g so the faulting address can be resolved to file:line afterwards; -O2 to
# match the product build closely enough for the lifetime bug to show up.
$exe = Join-Path $build 'dock_hash_lifetime.exe'
$src = Join-Path $here 'dock_hash_lifetime.cpp'
$logOut = Join-Path $build 'build.stdout.txt'
$logErr = Join-Path $build 'build.stderr.txt'
$args = @('-std=c++17', '-O2', '-g', '-fms-extensions', '-Wall', '-Wextra', '-I', $root, $src, '-o', $exe) + $qtFlags
$buildProc = Start-Process -FilePath $gpp -ArgumentList $args -NoNewWindow -Wait -PassThru -RedirectStandardOutput $logOut -RedirectStandardError $logErr
$buildText = ''
if (Test-Path $logOut) { $buildText += (Get-Content -Raw $logOut) }
if (Test-Path $logErr) { $buildText += (Get-Content -Raw $logErr) }
$warnings = @($buildText -split '\n' | Where-Object { $_ -match 'warning:' }).Count
if ($buildProc.ExitCode -ne 0) { Write-Host $buildText; throw 'compile failed' }
if ($warnings -ne 0) { Write-Host $buildText; throw ("warning gate failed: {0}" -f $warnings) }
Write-Host 'ok   dock_hash_lifetime.exe built, 0 warnings under -Wall -Wextra'

$rounds = 20
$modes = @('bug', 'bug_reused', 'ok', 'control')
$report = Join-Path $build 'crash.txt'
$summary = @()
foreach ($mode in $modes) {
    $crashes = 0
    $survived = 0
    $badCodes = @{}
    if (Test-Path $report) { Remove-Item $report -Force }
    for ($i = 1; $i -le $rounds; ++$i) {
        $out = Join-Path $build ('run_' + $mode + '_' + $i + '.txt')
        $proc = Start-Process -FilePath $exe -ArgumentList @($mode, $report) -NoNewWindow -Wait -PassThru -RedirectStandardOutput $out
        if ($proc.ExitCode -eq 0) {
            $survived++
        } else {
            $crashes++
            $code = ('{0}' -f $proc.ExitCode)
            if ($badCodes.ContainsKey($code)) { $badCodes[$code]++ } else { $badCodes[$code] = 1 }
        }
    }
    $detail = ($badCodes.GetEnumerator() | ForEach-Object { $_.Key + ' x' + $_.Value }) -join ', '
    Write-Host ('{0,-8} rodadas={1}  sobreviveu={2}  falhou={3}  {4}' -f $mode, $rounds, $survived, $crashes, $detail)
    $summary += [pscustomobject]@{ Modo = $mode; Rodadas = $rounds; Sobreviveu = $survived; Falhou = $crashes; Codigos = $detail }
    if (($mode -eq 'bug_reused') -and (Test-Path $report)) {
        $text = Get-Content -Raw $report
        Write-Host ''
        Write-Host '--- relatorio do primeiro crash (modo bug) ---'
        Write-Host $text
        $mEnd = [regex]::Match($text, 'endereco=(0x[0-9a-f]+)')
        $mBase = [regex]::Match($text, 'exe_base=(0x[0-9a-f]+)')
        if ($mEnd.Success -and $mBase.Success) {
            $end = [Convert]::ToUInt64($mEnd.Groups[1].Value, 16)
            $base = [Convert]::ToUInt64($mBase.Groups[1].Value, 16)
            $rva = $end - $base
            $link = 0x140000000 + $rva
            Write-Host ('endereco da falha: base+0x{0:x}  (link-time 0x{1:x})' -f $rva, $link)
            Write-Host '--- addr2line ---'
            $resolved = & $a2l -f -C -e $exe ('0x{0:x}' -f $link) 2>&1
            Write-Host ($resolved -join $nl)
        }
        Write-Host ''
    }
}
Write-Host ''
$summary | Format-Table -AutoSize | Out-String | Write-Host