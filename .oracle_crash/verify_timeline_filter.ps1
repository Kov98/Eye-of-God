# Requer -Version 5.1
# Confere o filtro da Timeline da GUI contra uma contagem INDEPENDENTE feita em
# PowerShell sobre o timeline.jsonl cru (nada do C++ participa do "esperado").
# Uso: powershell -File .oracle_crash\verify_timeline_filter.ps1
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
Set-Location $root
$dir = Join-Path $here 'sweep'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$caso = Join-Path $here 'filtro_caso'
if (-not (Test-Path (Join-Path $caso 'timeline.jsonl'))) {
    # Clone limpo nao tem a fixture: gera na hora, deterministico.
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'make_filter_fixture.ps1')
    if (-not (Test-Path (Join-Path $caso 'timeline.jsonl'))) { throw 'falha gerando a fixture' }
}
$shots = Join-Path $root '.oracle_gallery\shots'
New-Item -ItemType Directory -Force -Path $shots | Out-Null

function Count-Rows([string]$provider, [string]$channel, [string]$day, [bool]$cloudOnly) {
    $total = 0
    foreach ($f in @(Get-ChildItem -Path $caso -Recurse -Filter timeline.jsonl -File)) {
        $fromCloud = $f.FullName -match '\\cloud\\'
        foreach ($line in (Get-Content $f.FullName)) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            $row = $line | ConvertFrom-Json
            $ok = $true
            if ($provider -and $row.provider -ne $provider) { $ok = $false }
            if ($channel -and $row.channel -ne $channel) { $ok = $false }
            if ($cloudOnly -and -not $fromCloud) { $ok = $false }
            if ($day) {
                $local = [datetime]::Parse($row.timestamp_utc, $null,
                    [System.Globalization.DateTimeStyles]::RoundtripKind).ToLocalTime().ToString('yyyy-MM-dd')
                if ($local -ne $day) { $ok = $false }
            }
            if ($ok) { $total++ }
        }
    }
    return $total
}

$casos = @(
    @{ spec = '';                              esperado = (Count-Rows '' '' '' $false);           png = 'filtro_todos.png' },
    @{ spec = 'provider=Telegram';             esperado = (Count-Rows 'Telegram' '' '' $false);   png = 'filtro_telegram.png' },
    @{ spec = 'channel=WhatsApp/Messages';     esperado = (Count-Rows '' 'WhatsApp/Messages' '' $false); png = $null },
    @{ spec = 'from=2023-11-15;to=2023-11-15'; esperado = (Count-Rows '' '' '2023-11-15' $false); png = $null },
    @{ spec = 'cloud=1';                       esperado = (Count-Rows '' '' '' $true);            png = $null },
    @{ spec = 'from=2023-11-20;to=2023-11-20'; esperado = (Count-Rows '' '' '2023-11-20' $false); png = 'filtro_vazio.png' }
)
$falhas = 0
foreach ($c in $casos) {
    $png = Join-Path $dir 'tmp.png'
    if ($c.png) { $png = Join-Path $shots $c.png }
    $a = @('--screenshot', $png, '--case', $caso, '--tab', 'Timeline')
    if ($c.spec -ne '') { $a += @('--filter', $c.spec) }
    $o = Join-Path $dir 'out.txt'
    $e = Join-Path $dir 'err.txt'
    $p = Start-Process -FilePath (Join-Path $root 'OlhoDeDeus_gui.exe') -ArgumentList $a -NoNewWindow -Wait -PassThru -RedirectStandardOutput $o -RedirectStandardError $e
    $lines = @(Get-Content $o -ErrorAction SilentlyContinue)
    # O filtro e aplicado campo a campo (periodo, depois cada data): vale o ULTIMO
    # estado que a barra anunciou, nao um intermediario.
    $final = @($lines | Where-Object { $_ -match 'grafico:' }) | Select-Object -Last 1
    $got = -1
    if ($final -match 'grafico: (\d+) de (\d+) carregados') { $got = [int]$matches[1] }
    elseif ($final -match 'grafico: (\d+) eventos, sem filtro') { $got = [int]$matches[1] }
    $ok = ($p.ExitCode -eq 0) -and ($got -eq $c.esperado)
    if (-not $ok) { $falhas++ }
    $label = if ($c.spec -eq '') { '(sem filtro)' } else { $c.spec }
    Write-Host ('{0} {1,-32} independente={2,5}  gui={3,5}  exit={4}' -f $(if ($ok) { '[ok]  ' } else { '[FALHA]' }), $label, $c.esperado, $got, $p.ExitCode)
}
Write-Host ''
if ($falhas -ne 0) {
    Write-Host ('FALHOU: ' + $falhas + ' caso(s) divergentes')
    exit 1
}
Write-Host ('ok: ' + $casos.Count + ' filtros conferidos contra a contagem independente')
exit 0
