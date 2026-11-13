# Requires -Version 5.1
# Mede a taxa de falha no fechamento da GUI (docs/CRASHES-E-BUGS.md #9).
# Roda o modo evidencia (--screenshot), que sempre passa pelo teardown completo.
# Uso: powershell -File .oracle_crash\verify_crash_fix.ps1 [-Debug 40] [-Prod 20]
param(
    [int]$DebugRounds = 40,
    [int]$ProdRounds = 20
)
$ErrorActionPreference = 'Continue'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
Set-Location $root
$dir = Join-Path $here 'verify'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
Remove-Item (Join-Path $dir '*.txt') -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $root 'crash-*.txt') -Force -ErrorAction SilentlyContinue

$case = (Resolve-Path '.oracle/wa_tl').Path
$png = Join-Path $dir 'shot.png'

function Invoke-Round([string]$exe, [int]$rounds, [string]$label) {
    $ok = 0
    $codes = @{}
    $bad = @()
    for ($i = 1; $i -le $rounds; ++$i) {
        $o = Join-Path $dir ($label + '_out_' + $i + '.txt')
        $e = Join-Path $dir ($label + '_err_' + $i + '.txt')
        $p = Start-Process -FilePath $exe -ArgumentList @('--screenshot', $png, '--case', $case, '--tab', 'Timeline') -NoNewWindow -Wait -PassThru -RedirectStandardOutput $o -RedirectStandardError $e
        $code = ('{0}' -f $p.ExitCode)
        if ($p.ExitCode -eq 0) {
            $ok++
        } else {
            if ($codes.ContainsKey($code)) { $codes[$code]++ } else { $codes[$code] = 1 }
            $bad += [pscustomobject]@{ round = $i; code = $code; err = $e }
        }
    }
    $detail = ($codes.GetEnumerator() | ForEach-Object { $_.Key + ' x' + $_.Value }) -join ', '
    Write-Host ('{0,-8} rodadas={1}  ok={2}  falhou={3}  {4}' -f $label, $rounds, $ok, ($rounds - $ok), $detail)
    if ($bad.Count -gt 0) {
        $first = $bad[0]
        Write-Host ('  primeiro crash: rodada ' + $first.round + ' codigo ' + $first.code)
        @(Get-Content $first.err -ErrorAction SilentlyContinue) | Select-Object -Last 5 | ForEach-Object { Write-Host ('    ' + $_) }
    }
    return [pscustomobject]@{ Label = $label; Rounds = $rounds; Ok = $ok; Failed = ($rounds - $ok); Codes = $detail }
}

$results = @()
$results += Invoke-Round '.\OlhoDeDeus_gui_dbg.exe' $DebugRounds 'debug'
$results += Invoke-Round '.\OlhoDeDeus_gui.exe' $ProdRounds 'produto'

$reports = @(Get-ChildItem (Join-Path $root 'crash-*.txt') -ErrorAction SilentlyContinue)
Write-Host ('relatorios de crash gravados: ' + $reports.Count)
$totalFailed = ($results | Measure-Object -Property Failed -Sum).Sum
if ($reports.Count -gt 0) {
    Write-Host '--- primeiro relatorio ---'
    Get-Content $reports[0].FullName | ForEach-Object { Write-Host ('  ' + $_) }
}
Write-Host ''
$results | Format-Table -AutoSize | Out-String | Write-Host
if ($totalFailed -ne 0) {
    Write-Host ('FALHOU: ' + $totalFailed + ' fechamentos com crash')
    exit 1
}
Write-Host ('ok: ' + (($results | Measure-Object -Property Rounds -Sum).Sum) + ' fechamentos sem crash')
exit 0
