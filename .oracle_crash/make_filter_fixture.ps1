# Requer -Version 5.1
# Gera a fixture do filtro da Timeline: 2.519 eventos (2.019 de WhatsApp em 14/11 e
# 500 de Telegram em cloud/telegram em 15/11). Nada no dia 20 - e o caso do grafico
# vazio por filtro. Deterministico: rodar de novo da o mesmo arquivo.
# Uso: powershell -File .oracle_crash\make_filter_fixture.ps1
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$dir = Join-Path $here 'filtro_caso'
if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $dir 'cloud\telegram') | Out-Null

$lines = New-Object System.Collections.Generic.List[string]
for ($i = 1; $i -le 2019; ++$i) {
    $h = 10 + [int][math]::Floor($i / 240)
    $stamp = '2023-11-14T' + $h.ToString('00') + ':' + (($i * 7) % 60).ToString('00') + ':' + (($i * 13) % 60).ToString('00') + '.000Z'
    $lines.Add('{"timestamp_utc": "' + $stamp + '", "provider": "WhatsApp", "channel": "WhatsApp/Messages", "event_id": "w' + $i + '", "record_id": "rec-w' + $i + '", "event_user_sid": "5511999"}')
}
$lines2 = New-Object System.Collections.Generic.List[string]
for ($i = 1; $i -le 500; ++$i) {
    $h = 8 + [int][math]::Floor($i / 120)
    $stamp = '2023-11-15T' + $h.ToString('00') + ':' + (($i * 11) % 60).ToString('00') + ':' + (($i * 3) % 60).ToString('00') + '.000Z'
    $lines2.Add('{"timestamp_utc": "' + $stamp + '", "provider": "Telegram", "channel": "Telegram/Messages", "event_id": "t' + $i + '", "record_id": "rec-t' + $i + '", "event_user_sid": "5511888"}')
}
Set-Content -Path (Join-Path $dir 'timeline.jsonl') -Value $lines -Encoding UTF8
Set-Content -Path (Join-Path $dir 'cloud\telegram\timeline.jsonl') -Value $lines2 -Encoding UTF8
Write-Host ('fixture: ' + $lines.Count + ' whatsapp + ' + $lines2.Count + ' telegram em ' + $dir)
