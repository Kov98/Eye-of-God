$ErrorActionPreference = 'Continue'
$root = (Get-Location).Path
$dir = Join-Path $root '.oracle_crash\sweep'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
Remove-Item (Join-Path $root 'crash-*.txt') -Force -ErrorAction SilentlyContinue
$png = Join-Path $dir 'shot.png'
$casos = @(
  '.oracle/case_nuvem',
  '.oracle/deep_social',
  '.oracle/deep_takeout',
  '.oracle/deep_tg',
  '.oracle/deep_tkfull',
  '.oracle/tlx_case/caso-1',
  '.oracle/wa_tl'
)
$abas = @('Timeline', 'Galeria')
$vazio = Join-Path $dir 'caso_vazio'
New-Item -ItemType Directory -Force -Path $vazio | Out-Null
$ok = 0; $bad = 0
foreach ($c in $casos) {
  if (-not (Test-Path $c)) { Write-Host ('falta: ' + $c); continue }
  foreach ($aba in $abas) {
    $e = Join-Path $dir 'err.txt'
    $o = Join-Path $dir 'out.txt'
    $p = Start-Process -FilePath '.\OlhoDeDeus_gui.exe' -ArgumentList @('--screenshot', $png, '--case', (Resolve-Path $c).Path, '--tab', $aba) -NoNewWindow -Wait -PassThru -RedirectStandardOutput $o -RedirectStandardError $e
    $warn = @(Get-Content $e -ErrorAction SilentlyContinue)
    if ($p.ExitCode -eq 0) { $ok++ } else { $bad++; Write-Host ('CRASH ' + $c + ' ' + $aba + ' codigo ' + $p.ExitCode) }
    if ($warn.Count -gt 0) { Write-Host ('aviso em ' + $c + ' ' + $aba + ': ' + ($warn -join ' | ')) }
  }
}
$p2 = Start-Process -FilePath '.\OlhoDeDeus_gui.exe' -ArgumentList @('--screenshot', $png, '--case', $vazio, '--tab', 'Timeline') -NoNewWindow -Wait -PassThru
if ($p2.ExitCode -eq 0) { $ok++ } else { $bad++; Write-Host ('CRASH caso vazio codigo ' + $p2.ExitCode) }
$rep = @(Get-ChildItem (Join-Path $root 'crash-*.txt') -ErrorAction SilentlyContinue)
Write-Host ''
Write-Host ('rodadas=' + ($ok + $bad) + '  limpas=' + $ok + '  falhas=' + $bad + '  relatorios=' + $rep.Count)
if ($bad -gt 0) { exit 1 }