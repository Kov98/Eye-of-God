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
    # Aspas: a especificacao pode ter espaco (janela com hora), e Start-Process
    # junta a lista de argumentos sem cotar.
    if ($c.spec -ne '') { $a += @('--filter', ('"' + $c.spec + '"')) }
    $o = Join-Path $dir 'out.txt'
    $e = Join-Path $dir 'err.txt'
    $p = Start-Process -FilePath (Join-Path $root 'OlhoDeDeus_gui.exe') -ArgumentList $a -NoNewWindow -Wait -PassThru -RedirectStandardOutput $o -RedirectStandardError $e
    $lines = @(Get-Content $o -ErrorAction SilentlyContinue)
    # O filtro e aplicado campo a campo (periodo, depois cada data): vale o ULTIMO
    # estado que a barra anunciou, nao um intermediario.
    $final = @($lines | Where-Object { $_ -match 'timeline:' }) | Select-Object -Last 1
    $got = -1
    if ($final -match 'timeline: (\d+) de (\d+) carregados') { $got = [int]$matches[1] }
    elseif ($final -match 'timeline: (\d+) eventos, sem filtro') { $got = [int]$matches[1] }
    # A lista tem que ter exatamente as linhas que a barra anuncia.
    $listLine = @($lines | Where-Object { $_ -match 'lista: \d+ linhas' }) | Select-Object -Last 1
    $rows = -1
    if ($listLine -match 'lista: (\d+) linhas') { $rows = [int]$matches[1] }
    $ok = ($p.ExitCode -eq 0) -and ($got -eq $c.esperado) -and ($rows -eq $c.esperado)
    if (-not $ok) { $falhas++ }
    $label = if ($c.spec -eq '') { '(sem filtro)' } else { $c.spec }
    Write-Host ('{0} {1,-32} independente={2,5}  barra={3,5}  lista={4,5}  exit={5}' -f $(if ($ok) { '[ok]  ' } else { '[FALHA]' }), $label, $c.esperado, $got, $rows, $p.ExitCode)
}
Write-Host ''
Write-Host '--- export CSV do que esta filtrado ---'
# O CSV diz ser "o que esta na tela". Aqui ele e conferido contra a fonte: o mesmo
# filtro aplicado por PowerShell no timeline.jsonl cru.
$csv = Join-Path $dir 'export_filtrado.csv'
$meta = $csv + '.meta.json'
Remove-Item $csv, $meta, ($csv + '.tmp') -Force -ErrorAction SilentlyContinue
$o = Join-Path $dir 'out_export.txt'
$e = Join-Path $dir 'err_export.txt'
$p = Start-Process -FilePath (Join-Path $root 'OlhoDeDeus_gui.exe') -ArgumentList @('--screenshot', (Join-Path $dir 'tmp.png'), '--case', $caso, '--tab', 'Timeline', '--filter', '"provider=Telegram"', '--export-csv', $csv) -NoNewWindow -Wait -PassThru -RedirectStandardOutput $o -RedirectStandardError $e
$log = @(Get-Content $o -ErrorAction SilentlyContinue)
$exportLine = @($log | Where-Object { $_ -match 'export: linhas=' }) | Select-Object -Last 1
$exportOk = ($p.ExitCode -eq 0) -and ($exportLine -match 'linhas=(\d+)')
if (-not $exportOk) { $falhas++ }
Write-Host ('{0} GUI exportou  {1}' -f $(if ($exportOk) { '[ok]  ' } else { '[FALHA]' }), $exportLine)
if ($exportOk -and (Test-Path $csv)) {
    $rowsCsv = @(Import-Csv -Path $csv)
    $sameCount = ($rowsCsv.Count -eq 500)
    if (-not $sameCount) { $falhas++ }
    Write-Host ('{0} CSV tem 500 linhas  ({1})' -f $(if ($sameCount) { '[ok]  ' } else { '[FALHA]' }), $rowsCsv.Count)

    # Ids do CSV x ids do arquivo cru com o mesmo filtro.
    $esperados = New-Object System.Collections.Generic.List[string]
    foreach ($f in @(Get-ChildItem -Path $caso -Recurse -Filter timeline.jsonl -File)) {
        foreach ($line in (Get-Content $f.FullName)) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            $row = $line | ConvertFrom-Json
            if ($row.provider -eq 'Telegram') { $esperados.Add([string]$row.event_id) }
        }
    }
    $csvIds = @($rowsCsv | ForEach-Object { $_.event_id })
    $diff = Compare-Object -ReferenceObject $esperados -DifferenceObject $csvIds
    $sameIds = ($null -eq $diff)
    if (-not $sameIds) { $falhas++ }
    Write-Host ('{0} mesmos event_id do cru  (csv={1} cru={2})' -f $(if ($sameIds) { '[ok]  ' } else { '[FALHA]' }), $csvIds.Count, $esperados.Count)

    $hashFile = (Get-FileHash -Path $csv -Algorithm SHA256).Hash.ToLower()
    $metaJson = Get-Content -Raw $meta | ConvertFrom-Json
    $hashOk = ($hashFile -eq $metaJson.sha256_csv)
    if (-not $hashOk) { $falhas++ }
    Write-Host ('{0} sha256 do sidecar bate com o arquivo  ({1}...)' -f $(if ($hashOk) { '[ok]  ' } else { '[FALHA]' }), $hashFile.Substring(0, 12))

    $filterOk = ($metaJson.filter -eq 'provider=Telegram') -and ($metaJson.rows -eq 500)
    if (-not $filterOk) { $falhas++ }
    Write-Host ('{0} sidecar registra filtro e linhas  ({1} / {2})' -f $(if ($filterOk) { '[ok]  ' } else { '[FALHA]' }), $metaJson.filter, $metaJson.rows)

    $tmpOk = -not (Test-Path ($csv + '.tmp'))
    if (-not $tmpOk) { $falhas++ }
    Write-Host ('{0} sem .tmp para tras' -f $(if ($tmpOk) { '[ok]  ' } else { '[FALHA]' }))
} else {
    $falhas++
    Write-Host '[FALHA] CSV nao foi escrito'
}

Write-Host ''
Write-Host '--- export JSONL: linha original conferida contra a fonte ---'
$jsonl = Join-Path $dir 'export_filtrado.jsonl'
Remove-Item $jsonl, ($jsonl + '.meta.json'), ($jsonl + '.tmp') -Force -ErrorAction SilentlyContinue
$o = Join-Path $dir 'out_jsonl.txt'
$p = Start-Process -FilePath (Join-Path $root 'OlhoDeDeus_gui.exe') -ArgumentList @('--screenshot', (Join-Path $dir 'tmp.png'), '--case', $caso, '--tab', 'Timeline', '--filter', '"provider=Telegram"', '--export-jsonl', $jsonl) -NoNewWindow -Wait -PassThru -RedirectStandardOutput $o -RedirectStandardError (Join-Path $dir 'err_jsonl.txt')
$log = @(Get-Content $o -ErrorAction SilentlyContinue)
$linhaExport = @($log | Where-Object { $_ -match 'formato=jsonl' }) | Select-Object -Last 1
$exportOk = ($p.ExitCode -eq 0) -and ($linhaExport -match 'linhas=(\d+)')
if (-not $exportOk) { $falhas++ }
Write-Host ('{0} GUI exportou JSONL  {1}' -f $(if ($exportOk) { '[ok]  ' } else { '[FALHA]' }), $linhaExport)

if ($exportOk -and (Test-Path $jsonl)) {
    $linhasJsonl = @(Get-Content -Path $jsonl -Encoding UTF8)
    # Linhas da fonte com o mesmo filtro, lidas do arquivo cru.
    $linhasFonte = New-Object System.Collections.Generic.List[string]
    foreach ($f in @(Get-ChildItem -Path $caso -Recurse -Filter timeline.jsonl -File)) {
        foreach ($line in (Get-Content -Path $f.FullName -Encoding UTF8)) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            $row = $line | ConvertFrom-Json
            if ($row.provider -eq 'Telegram') { $linhasFonte.Add($line) }
        }
    }
    $countOk = ($linhasJsonl.Count -eq 500) -and ($linhasFonte.Count -eq 500)
    if (-not $countOk) { $falhas++ }
    Write-Host ('{0} 500 linhas nos dois lados  (jsonl={1} fonte={2})' -f $(if ($countOk) { '[ok]  ' } else { '[FALHA]' }), $linhasJsonl.Count, $linhasFonte.Count)

    # Identidade linha a linha: e a prova de que o export nao reescreveu a evidencia.
    $diff = Compare-Object -ReferenceObject $linhasFonte -DifferenceObject $linhasJsonl
    $sameLines = ($null -eq $diff)
    if (-not $sameLines) { $falhas++ }
    Write-Host ('{0} conteudo identico linha a linha  (diferencas={1})' -f $(if ($sameLines) { '[ok]  ' } else { '[FALHA]' }), @($diff).Count)

    $hashFile = (Get-FileHash -Path $jsonl -Algorithm SHA256).Hash.ToLower()
    $metaJson = Get-Content -Raw ($jsonl + '.meta.json') | ConvertFrom-Json
    $hashOk = ($hashFile -eq $metaJson.sha256_jsonl) -and ($metaJson.rows -eq 500)
    if (-not $hashOk) { $falhas++ }
    Write-Host ('{0} sidecar do JSONL bate  ({1}... / {2} linhas)' -f $(if ($hashOk) { '[ok]  ' } else { '[FALHA]' }), $hashFile.Substring(0, 12), $metaJson.rows)

    $tmpOk = -not (Test-Path ($jsonl + '.tmp'))
    if (-not $tmpOk) { $falhas++ }
    Write-Host ('{0} jsonl sem .tmp para tras' -f $(if ($tmpOk) { '[ok]  ' } else { '[FALHA]' }))
} else {
    $falhas++
    Write-Host '[FALHA] JSONL nao foi escrito'
}

Write-Host ''
Write-Host '--- janela com HORA (o filtro deixou de ser por dia) ---'
function Count-Between([datetime]$fromLocal, [datetime]$toLocal) {
    $total = 0
    foreach ($f in @(Get-ChildItem -Path $caso -Recurse -Filter timeline.jsonl -File)) {
        foreach ($line in (Get-Content $f.FullName)) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            $row = $line | ConvertFrom-Json
            $local = [datetime]::Parse($row.timestamp_utc, $null, [System.Globalization.DateTimeStyles]::RoundtripKind).ToLocalTime()
            if ($local -ge $fromLocal -and $local -le $toLocal) { $total++ }
        }
    }
    return $total
}
$janelas = @(
    @{ from = '2023-11-15 08:00'; to = '2023-11-15 08:30' },
    @{ from = '2023-11-15 07:00'; to = '2023-11-15 07:05' },
    @{ from = '2023-11-15 00:00'; to = '2023-11-15 23:59' }
)
foreach ($janela in $janelas) {
    $desde = [datetime]::ParseExact($janela.from, 'yyyy-MM-dd HH:mm', [System.Globalization.CultureInfo]::InvariantCulture)
    $ate = [datetime]::ParseExact($janela.to, 'yyyy-MM-dd HH:mm', [System.Globalization.CultureInfo]::InvariantCulture)
    $esperado = Count-Between $desde $ate
    $spec = 'from=' + $janela.from + ';to=' + $janela.to
    $o = Join-Path $dir 'out_min.txt'
    $p = Start-Process -FilePath (Join-Path $root 'OlhoDeDeus_gui.exe') -ArgumentList @('--screenshot', (Join-Path $dir 'tmp.png'), '--case', $caso, '--tab', 'Timeline', '--filter', ('"' + $spec + '"')) -NoNewWindow -Wait -PassThru -RedirectStandardOutput $o -RedirectStandardError (Join-Path $dir 'err_min.txt')
    $log = @(Get-Content $o -ErrorAction SilentlyContinue)
    $final = @($log | Where-Object { $_ -match 'timeline: ' }) | Select-Object -Last 1
    $got = -1
    if ($final -match 'timeline: (\d+) de (\d+) carregados') { $got = [int]$matches[1] }
    elseif ($final -match 'timeline: (\d+) eventos, sem filtro') { $got = [int]$matches[1] }
    $ok = ($p.ExitCode -eq 0) -and ($got -eq $esperado)
    if (-not $ok) { $falhas++ }
    Write-Host ('{0} {1,-42} independente={2,5}  gui={3,5}' -f $(if ($ok) { '[ok]  ' } else { '[FALHA]' }), $spec, $esperado, $got)
}

Write-Host ''
Write-Host '--- arraste no grafico recorta o periodo ---'
$o = Join-Path $dir 'out_drag.txt'
$p = Start-Process -FilePath (Join-Path $root 'OlhoDeDeus_gui.exe') -ArgumentList @('--screenshot', (Join-Path $dir 'tmp.png'), '--case', $caso, '--tab', 'Timeline', '--filter', '"provider=Telegram"', '--drag-rows', '0', '100') -NoNewWindow -Wait -PassThru -RedirectStandardOutput $o -RedirectStandardError (Join-Path $dir 'err_drag.txt')
$log = @(Get-Content $o -ErrorAction SilentlyContinue)
$arraste = @($log | Where-Object { $_ -match 'arraste: linhas' }) | Select-Object -Last 1
$perOk = $false
$esperadoDrag = -1
$guiDrag = -1
if ($arraste -match 'periodo=(.+?)\.\.(.+?) lista=(-?\d+)') {
    $desdeStr = $matches[1]
    $ateStr = $matches[2]
    $guiDrag = [int]$matches[3]
    $desde = [datetime]::ParseExact($desdeStr, 'yyyy-MM-dd HH:mm:ss', [System.Globalization.CultureInfo]::InvariantCulture)
    $ate = [datetime]::ParseExact($ateStr, 'yyyy-MM-dd HH:mm:ss', [System.Globalization.CultureInfo]::InvariantCulture)
    $esperadoDrag = 0
    foreach ($f in @(Get-ChildItem -Path $caso -Recurse -Filter timeline.jsonl -File)) {
        foreach ($line in (Get-Content $f.FullName)) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            $row = $line | ConvertFrom-Json
            if ($row.provider -ne 'Telegram') { continue }
            $local = [datetime]::Parse($row.timestamp_utc, $null, [System.Globalization.DateTimeStyles]::RoundtripKind).ToLocalTime()
            if ($local -ge $desde -and $local -le $ate) { $esperadoDrag++ }
        }
    }
    $perOk = ($guiDrag -eq $esperadoDrag) -and ($guiDrag -gt 0)
}
if (-not $perOk) { $falhas++ }
Write-Host ('{0} periodo aplicado pelo arraste  {1}' -f $(if ($perOk) { '[ok]  ' } else { '[FALHA]' }), $arraste)
Write-Host ('        independente={0}  gui={1}' -f $esperadoDrag, $guiDrag)
$tmpTocado = $false
if ($arraste) { $tmpTocado = $true }
# O arraste tem que ter mudado o filtro de periodo: a barra passa a mostrar Periodo marcado.
$periodoMarcado = @($log | Where-Object { $_ -match 'arraste: ' }).Count -gt 0
if (-not $periodoMarcado -or -not $tmpTocado) { $falhas++ }

Write-Host ''
Write-Host '--- clique no grafico pula para o evento ---'
# Ids na ordem cronologica, calculados do arquivo cru (o C++ nao participa).
$ordenados = New-Object System.Collections.Generic.List[object]
foreach ($f in @(Get-ChildItem -Path $caso -Recurse -Filter timeline.jsonl -File)) {
    foreach ($line in (Get-Content $f.FullName)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $row = $line | ConvertFrom-Json
        if ($row.provider -ne 'Telegram') { continue }
        # Ordem estavel pedida por (timestamp, indice de leitura): o PowerShell 5.1
        # nao tem -Stable, e Sort-Object por duas chaves da o mesmo resultado.
        $ordenados.Add([pscustomobject]@{
            id = [string]$row.event_id
            ts = [datetime]::Parse($row.timestamp_utc, $null, [System.Globalization.DateTimeStyles]::RoundtripKind)
            idx = $ordenados.Count
        })
    }
}
$cronologico = @($ordenados | Sort-Object -Property ts, idx | ForEach-Object { $_.id })
Write-Host ('    eventos Telegram em ordem cronologica: ' + $cronologico.Count)

foreach ($alvo in @(0, -1)) {
    $o = Join-Path $dir 'out_click.txt'
    $p = Start-Process -FilePath (Join-Path $root 'OlhoDeDeus_gui.exe') -ArgumentList @('--screenshot', (Join-Path $dir 'tmp.png'), '--case', $caso, '--tab', 'Timeline', '--filter', '"provider=Telegram"', '--click-row', $alvo) -NoNewWindow -Wait -PassThru -RedirectStandardOutput $o -RedirectStandardError (Join-Path $dir 'err_click.txt')
    $log = @(Get-Content $o -ErrorAction SilentlyContinue)
    $line = @($log | Where-Object { $_ -match 'clique: pedido=' }) | Select-Object -Last 1
    $got = -1; $drawn = -1; $id = ''
    if ($line -match 'pedido=(-?\d+) desenhado=(-?\d+) obtido=(-?\d+) evento=(\S*)') {
        $drawn = [int]$matches[2]
        $got = [int]$matches[3]
        $id = $matches[4]
    }
    $esperadoId = if ($drawn -ge 0 -and $drawn -lt $cronologico.Count) { $cronologico[$drawn] } else { '' }
    $ok = ($p.ExitCode -eq 0) -and ($got -eq $drawn) -and ($id -eq $esperadoId) -and $esperadoId
    if (-not $ok) { $falhas++ }
    Write-Host ('{0} clique pedido={1,-3} desenhado={2,-3} obtido={3,-3} evento={4}' -f $(if ($ok) { '[ok]  ' } else { '[FALHA]' }), $alvo, $drawn, $got, $id)
    if (-not $ok) { Write-Host ('        esperado=' + $esperadoId) }
}

Write-Host ''
Write-Host '--- laudo cita o recorte e confere o hash dele ---'
$pdf = Join-Path $dir 'laudo_recorte.pdf'
$pdfTampered = Join-Path $dir 'laudo_recorte_adulterado.pdf'
Remove-Item $pdf, $pdfTampered -Force -ErrorAction SilentlyContinue
$o = Join-Path $dir 'out_report.txt'
$p = Start-Process -FilePath (Join-Path $root 'olho_report.exe') -ArgumentList @($caso, '--out', $pdf, '--recorte', $csv) -NoNewWindow -Wait -PassThru -RedirectStandardOutput $o -RedirectStandardError (Join-Path $dir 'err_report.txt')
$log = @(Get-Content $o -ErrorAction SilentlyContinue)
$citou = ($p.ExitCode -eq 0) -and (@($log | Where-Object { $_ -match 'recorte: .* sha256 confirmado' }).Count -eq 1) -and (Test-Path $pdf)
if (-not $citou) { $falhas++ }
Write-Host ('{0} laudo com recorte saiu e citou o hash  (exit={1})' -f $(if ($citou) { '[ok]  ' } else { '[FALHA]' }), $p.ExitCode)
if (-not $citou) { @($log | Select-Object -Last 4) | ForEach-Object { Write-Host ('        ' + $_) } }

# Recorte adulterado: o laudo tem que ser RECUSADO (exit 4) e nao escrever PDF.
$copiaCsv = Join-Path $dir 'recorte_adulterado.csv'
Copy-Item $csv $copiaCsv -Force
Copy-Item ($csv + '.meta.json') ($copiaCsv + '.meta.json') -Force
$conteudo = [System.IO.File]::ReadAllText($copiaCsv)
[System.IO.File]::WriteAllText($copiaCsv, $conteudo + " ")
$o2 = Join-Path $dir 'out_report_tamper.txt'
$p2 = Start-Process -FilePath (Join-Path $root 'olho_report.exe') -ArgumentList @($caso, '--out', $pdfTampered, '--recorte', $copiaCsv) -NoNewWindow -Wait -PassThru -RedirectStandardOutput $o2 -RedirectStandardError (Join-Path $dir 'err_report_tamper.txt')
$recusou = ($p2.ExitCode -eq 4) -and (-not (Test-Path $pdfTampered))
if (-not $recusou) { $falhas++ }
$linhaRecusa = @(Get-Content $o2 -ErrorAction SilentlyContinue | Where-Object { $_ -match 'nao bate com o sidecar|LAUDO RECUSADO' }) | Select-Object -First 1
Write-Host ('{0} recorte adulterado recusa o laudo  (exit={1})  {2}' -f $(if ($recusou) { '[ok]  ' } else { '[FALHA]' }), $p2.ExitCode, $linhaRecusa)

# Recorte em JSONL usa a outra chave do sidecar (sha256_jsonl).
if (Test-Path $jsonl) {
    $pdf2 = Join-Path $dir 'laudo_recorte_jsonl.pdf'
    Remove-Item $pdf2 -Force -ErrorAction SilentlyContinue
    $o3 = Join-Path $dir 'out_report_jsonl.txt'
    $p3 = Start-Process -FilePath (Join-Path $root 'olho_report.exe') -ArgumentList @($caso, '--out', $pdf2, '--recorte', $jsonl) -NoNewWindow -Wait -PassThru -RedirectStandardOutput $o3 -RedirectStandardError (Join-Path $dir 'err_report_jsonl.txt')
    $jsonlOk = ($p3.ExitCode -eq 0) -and (Test-Path $pdf2)
    if (-not $jsonlOk) { $falhas++ }
    Write-Host ('{0} recorte JSONL tambem passa  (exit={1})' -f $(if ($jsonlOk) { '[ok]  ' } else { '[FALHA]' }), $p3.ExitCode)
}

Write-Host ''
if ($falhas -ne 0) {
    Write-Host ('FALHOU: ' + $falhas + ' divergencia(s)')
    exit 1
}
Write-Host ('ok: ' + $casos.Count + ' filtros + export CSV + clique conferidos contra a contagem independente')
exit 0
