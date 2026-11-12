#Requires -Version 5.1
# Recuperacao de WhatsApp ponta a ponta: fixture -> msgstore cifrado -> wa_recover
# -> report.json -> marca de midia efemera (ver uma vez).
#
# A fixture e regerada a cada rodada e o banco e cifrado pelo .NET em
# encrypt_full.ps1, nao pelo codigo C++ sob teste: se o mesmo codigo cifrasse e
# decifrasse, um erro simetrico passaria despercebido.
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
$exe = Join-Path $root 'wa_recover.exe'
$out = Join-Path $here 'wa_e2e'

$script:checks = 0
$script:failures = 0
function Check([string]$what, [bool]$ok, [string]$detail) {
    $script:checks++
    if (-not $ok) { $script:failures++ }
    $tag = if ($ok) { '[ok]   ' } else { '[FALHA]' }
    $suffix = if ($detail) { '  (' + $detail + ')' } else { '' }
    Write-Host ($tag + ' ' + $what + $suffix)
}

if (-not (Test-Path $exe)) {
    Write-Host 'wa_recover.exe ausente: rode build_deep.ps1'
    exit 2
}
$sqliteObj = Join-Path $root 'third_party\sqlite\sqlite3.o'
if (-not (Test-Path $sqliteObj)) { Write-Host 'sqlite3.o ausente: rode build_deep.ps1'; exit 2 }

# 1. fixture
$gpp = 'C:\msys64\ucrt64\bin\g++.exe'
if (-not (Test-Path $gpp)) { $gpp = (Get-Command g++ -ErrorAction Stop).Source }
$mkfull = Join-Path $here 'mkfull.exe'
& $gpp -std=c++17 -O2 -I $root -I (Join-Path $root 'third_party') -I (Join-Path $root 'third_party\sqlite') (Join-Path $here 'make_full.cpp') $sqliteObj -o $mkfull 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Host 'mkfull: falha ao compilar a fixture'; exit 2 }
& $mkfull | Out-Null
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'encrypt_full.ps1') | Out-Null
Check 'fixture cifrada pelo .NET (SQLCipher)' (Test-Path (Join-Path $here 'full.db.enc')) ''

# 2. recuperacao
Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
$key = (Get-Content -Raw (Join-Path $here 'full_key.txt')).Trim()
$log = & $exe (Join-Path $here 'full.db.enc') --key $key --out $out --media $here 2>&1
$code = $LASTEXITCODE
Check 'wa_recover sai com 0' ($code -eq 0) ('exit=' + $code)
$reportPath = Join-Path $out 'report.json'
Check 'report.json escrito' (Test-Path $reportPath) ''
if (-not (Test-Path $reportPath)) {
    Write-Host ''
    Write-Host ('wa_e2e: ' + $script:checks + ' verificacoes, ' + $script:failures + ' falha(s)')
    Write-Host ($log | Out-String)
    exit 1
}
$report = Get-Content -Raw $reportPath | ConvertFrom-Json

# 3. o relatorio
Check 'duas linhas de midia no banco' ($report.midia_ok -eq 2) ('midia_ok=' + $report.midia_ok)
Check 'uma linha marcada ver-uma-vez' ($report.midia_view_once -eq 1) ('midia_view_once=' + $report.midia_view_once)
Check 'tabela dedicada de efemera detectada' ($report.midia_view_once_tabela -eq $true) ''
Check 'um arquivo efemero no relatorio' ($report.midia_view_once_arquivos -eq 1) ('arquivos=' + $report.midia_view_once_arquivos)

$entries = @($report.midia)
Check 'lista por arquivo com 2 entradas' ($entries.Count -eq 2) ('entradas=' + $entries.Count)
$flagged = @($entries | Where-Object { $_.view_once -eq $true })
Check 'exatamente uma entrada com view_once' ($flagged.Count -eq 1) ('marcadas=' + $flagged.Count)
$flaggedName = if ($flagged.Count -eq 1) { $flagged[0].file } else { 'n/a' }
Check 'a efemera e a 2_media2.jpg' ($flaggedName -eq '2_media2.jpg') $flaggedName
$plainFlagged = @($entries | Where-Object { $_.file -eq '1_media1.jpg' -and $_.view_once -eq $true })
Check 'a midia comum nao carrega a marca' ($plainFlagged.Count -eq 0) ''

# 4. bytes: o que saiu tem que ser byte a byte o original
foreach ($i in 1, 2) {
    $got = Join-Path $out ('media/' + $i + '_media' + $i + '.jpg')
    $want = Join-Path $here ('orig' + $i + '.jpg')
    $same = (Test-Path $got) -and ((Get-FileHash $want -Algorithm SHA256).Hash -eq (Get-FileHash $got -Algorithm SHA256).Hash)
    Check ('midia ' + $i + ' identica ao original') $same ''
}


# 5. linha do tempo: e o que a aba Timeline da GUI desenha. Formato e chaves tem
# que ser os mesmos que os importadores de nuvem/social escrevem.
$timelinePath = Join-Path $out "timeline.jsonl"
Check "timeline.jsonl escrito" (Test-Path $timelinePath) ""
$events = @(Get-Content $timelinePath | Where-Object { $_.Trim() -ne "" })
Check "um evento por mensagem viva" ($events.Count -eq $report.timeline_eventos -and $events.Count -gt 100) ("eventos=" + $events.Count + " report=" + $report.timeline_eventos)
$firstEvent = $events[0] | ConvertFrom-Json
Check "evento no formato que a GUI le" ($firstEvent.provider -eq "WhatsApp" -and $firstEvent.channel -eq "WhatsApp/Messages" -and $firstEvent.timestamp_utc -match "^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$") ("provider=" + $firstEvent.provider + " ts=" + $firstEvent.timestamp_utc)
$stamps = @($events | ForEach-Object { $_ | ConvertFrom-Json } | ForEach-Object { $_.timestamp_utc })
$sorted = @($stamps | Sort-Object)
Check "eventos em ordem de tempo" (($stamps -join "|") -eq ($sorted -join "|")) ("primeiro=" + $stamps[0] + " ultimo=" + $stamps[-1])

Write-Host ''
Write-Host ('wa_e2e: ' + $script:checks + ' verificacoes, ' + $script:failures + ' falha(s)')
if ($script:failures -ne 0) { exit 1 }
exit 0