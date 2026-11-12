<#
.SYNOPSIS
    render.ps1 - tenta renderizar um arquivo (PDF) com o Edge headless e salva um PNG.

.DESCRIPTION
    Leva o arquivo a um leitor real (Chromium/PDFium dentro do Edge) e salva um PNG.
    Reporta exatamente o que aconteceu. NAO inventa resultado.

    Medicoes que o script imprime:
      - caminho do Edge usado, versao do modo headless tentado
      - exit code do processo, tamanho do PNG
      - dimensoes lidas do cabecalho IHDR do PNG (sem depender de biblioteca)
      - cor dominante da imagem e fracao de pixels que dela diferem
        (uma tela chapada tem fracao ~0: o PDF nao foi desenhado)
      - stderr do Edge

    Veredito (exit code):
      0 = RENDER OK        imagem escrita, com regiao nao-chapada (>0.5% dos pixels)
      1 = RENDER FALHOU    nenhum PNG foi escrito
      2 = RENDER VAZIO     PNG escrito mas a imagem e uma tela chapada

    LIMITE CONHECIDO: o Chromium headless nao carrega a extensao visualizadora de
    PDF nesta maquina; o resultado e uma tela chapada. Quando isso acontece o
    script diz RENDER VAZIO em vez de apresentar o PNG como prova. Use o
    pdfcheck.py para conformidade e valide o visual com o Edge interativo.

.PARAMETER Pdf
    Caminho do arquivo de entrada (PDF; HTML tambem serve, como controle).

.PARAMETER Png
    Caminho do PNG de saida.

.EXAMPLE
    powershell -NoProfile -File render.ps1 -Pdf out\valid_classic.pdf -Png out\render_valid.png
#>
param(
    [Parameter(Mandatory = $true)][string]$Pdf,
    [Parameter(Mandatory = $true)][string]$Png,
    [int]$Width = 1240,
    [int]$Height = 1754,
    [int]$TimeoutSeg = 90
)

$ErrorActionPreference = 'Continue'
function Falhar([string]$msg) { Write-Output "RENDER FALHOU: $msg"; exit 1 }

# Um PNG valido termina no chunk IEND. O Chromium grava um arquivo provisorio
# antes do definitivo, entao testar o tamanho sozinho nao basta.
function Test-PngCompleto([string]$caminho) {
    if (-not (Test-Path -LiteralPath $caminho)) { return $false }
    try {
        $fs = [System.IO.File]::OpenRead($caminho)
        $len = $fs.Length
        if ($len -lt 16) { $fs.Close(); return $false }
        $fs.Seek($len - 8, [System.IO.SeekOrigin]::Begin) | Out-Null
        $buf = New-Object byte[] 8
        [void]$fs.Read($buf, 0, 8)
        $fs.Close()
        # os ultimos 8 bytes de um PNG sao o tipo 'IEND' seguido do CRC: o tipo
        # comeca no offset 0 do buffer lido de (len - 8)
        $tipo = [System.Text.Encoding]::ASCII.GetString($buf, 0, 4)
        return ($tipo -eq 'IEND')
    } catch {
        return $false
    }
}

if (-not (Test-Path -LiteralPath $Pdf)) { Falhar "entrada nao existe: $Pdf" }
$inItem = Get-Item -LiteralPath $Pdf
$PngDir = Split-Path -Parent $Png
if ($PngDir -and -not (Test-Path -LiteralPath $PngDir)) { Falhar "diretorio de saida nao existe: $PngDir" }
$PngAbs = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Png))

$pf86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
$pf = [Environment]::GetEnvironmentVariable('ProgramFiles')
$lad = [Environment]::GetEnvironmentVariable('LOCALAPPDATA')
$candidatos = @(
    @(
        (Join-Path $pf86 'Microsoft\Edge\Application\msedge.exe'),
        (Join-Path $pf 'Microsoft\Edge\Application\msedge.exe'),
        (Join-Path $lad 'Microsoft\Edge\Application\msedge.exe')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
)
if ($candidatos.Count -eq 0) { Falhar "msedge.exe nao encontrado em Program Files (x86), Program Files nem LOCALAPPDATA" }
$edge = $candidatos[0]

$ext = [System.IO.Path]::GetExtension($inItem.Name).ToLowerInvariant()
Write-Output "entrada      : $($inItem.FullName)"
Write-Output "bytes        : $($inItem.Length)"
Write-Output "tipo         : $ext"
Write-Output "edge         : $edge"
Write-Output "saida png    : $PngAbs"
Write-Output ("viewport     : " + $Width + "x" + $Height)

if (Test-Path -LiteralPath $PngAbs) { Remove-Item -LiteralPath $PngAbs -Force }

$uri = ([System.Uri]([System.IO.Path]::GetFullPath($inItem.FullName))).AbsoluteUri
$stderrFile = [System.IO.Path]::GetTempFileName()

$tentativas = @('--headless=new', '--headless')
$escrito = $false
foreach ($modo in $tentativas) {
    $argList = @()
    $argList += $modo
    $argList += '--disable-gpu'
    $argList += '--hide-scrollbars'
    $argList += '--no-first-run'
    $argList += '--no-default-browser-check'
    $argList += '--disable-extensions'
    $argList += ('--window-size=' + $Width + ',' + $Height)
    $argList += ('--screenshot=' + $PngAbs)
    $argList += $uri
    Write-Output "tentativa    : $modo"
    $p = Start-Process -FilePath $edge -ArgumentList $argList -NoNewWindow -PassThru -RedirectStandardError $stderrFile -ErrorAction SilentlyContinue
    if (-not $p) { Write-Output "             : nao foi possivel iniciar o processo"; continue }
    $fim = (Get-Date).AddSeconds($TimeoutSeg)
    while (-not $p.HasExited -and (Get-Date) -lt $fim) { Start-Sleep -Milliseconds 200 }
    if (-not $p.HasExited) {
        try { $p.Kill() } catch {}
        Write-Output "             : timeout de $TimeoutSeg s, processo encerrado"
    } else {
        Write-Output "             : exit code $($p.ExitCode)"
    }
    # espera o tamanho do arquivo estabilizar. O Chromium escreve um PNG
    # provisorio pequeno e depois o definitivo; 1.4 s estavel evita ler o provisorio.
    $ultimo = -1
    $estaveis = 0
    $limite = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $limite) {
        if (Test-Path -LiteralPath $PngAbs) {
            $tam = (Get-Item -LiteralPath $PngAbs).Length
            if ($tam -gt 0 -and $tam -eq $ultimo -and (Test-PngCompleto $PngAbs)) {
                $estaveis++
                if ($estaveis -ge 3) { break }
            } else {
                $estaveis = 0
            }
            $ultimo = $tam
        }
        Start-Sleep -Milliseconds 200
    }
    if (Test-PngCompleto $PngAbs) { $escrito = $true; break }
    if (Test-Path -LiteralPath $PngAbs) {
        $tamParcial = (Get-Item -LiteralPath $PngAbs).Length
        Write-Output "             : PNG provisorio de $tamParcial bytes, sem IEND (descartado)"
        Remove-Item -LiteralPath $PngAbs -Force -ErrorAction SilentlyContinue
    }
}

$errTxt = ''
if (Test-Path -LiteralPath $stderrFile) { $errTxt = (Get-Content -LiteralPath $stderrFile -Raw -ErrorAction SilentlyContinue) }
Remove-Item -LiteralPath $stderrFile -Force -ErrorAction SilentlyContinue

if (-not $escrito) {
    if ($errTxt) { Write-Output "stderr edge  : $($errTxt.Trim())" }
    if ($ext -eq '.pdf') {
        Falhar ("nenhum PNG completo foi escrito em $TimeoutSeg s por tentativa. " +
                "O Edge headless desta maquina nao renderiza o conteudo do PDF.")
    }
    Falhar "nenhum PNG completo foi escrito."
}

$completo = Test-PngCompleto $PngAbs
Write-Output "IEND         : presente = $completo"
if (-not $completo) {
    Write-Output "RENDER VAZIO: o PNG esta incompleto (sem chunk IEND): o Edge nao terminou de escrever."
    exit 2
}

# dimensoes do IHDR, sem biblioteca
$dim = $null
try {
    $fs = [System.IO.File]::OpenRead($PngAbs)
    $buf = New-Object byte[] 26
    [void]$fs.Read($buf, 0, 26)
    $fs.Close()
    $w = [int]$buf[16] * 16777216 + [int]$buf[17] * 65536 + [int]$buf[18] * 256 + [int]$buf[19]
    $h = [int]$buf[20] * 16777216 + [int]$buf[21] * 65536 + [int]$buf[22] * 256 + [int]$buf[23]
    $dim = @($w, $h)
    Write-Output "IHDR         : $w x $h, profundidade $($buf[24]), tipo de cor $($buf[25])"
} catch {
    Write-Output "IHDR         : nao lido ($($_.Exception.Message))"
}

$fracDesvio = $null
$dominante = $null
$amostrados = 0
try {
    Add-Type -AssemblyName System.Drawing -ErrorAction Stop
    $bmp = [System.Drawing.Bitmap]::FromFile($PngAbs)
    $cont = @{}
    $stepX = [Math]::Max(1, [int]($bmp.Width / 160))
    $stepY = [Math]::Max(1, [int]($bmp.Height / 200))
    $amostras = New-Object System.Collections.ArrayList
    for ($y = 0; $y -lt $bmp.Height; $y += $stepY) {
        for ($x = 0; $x -lt $bmp.Width; $x += $stepX) {
            $c = $bmp.GetPixel($x, $y)
            $key = ('' + $c.R + ',' + $c.G + ',' + $c.B)
            [void]$amostras.Add(@($c.R, $c.G, $c.B))
            if ($cont.ContainsKey($key)) { $cont[$key] = $cont[$key] + 1 } else { $cont[$key] = 1 }
        }
    }
    $bmp.Dispose()
    $amostrados = $amostras.Count
    $melhorKey = $null
    $melhorN = -1
    foreach ($k in $cont.Keys) {
        if ($cont[$k] -gt $melhorN) { $melhorN = $cont[$k]; $melhorKey = $k }
    }
    $dominante = $melhorKey
    $p = $melhorKey.Split(',')
    $dr = [int]$p[0]; $dg = [int]$p[1]; $db = [int]$p[2]
    $desvio = 0
    foreach ($s in $amostras) {
        if ([Math]::Abs($s[0] - $dr) -gt 16 -or [Math]::Abs($s[1] - $dg) -gt 16 -or [Math]::Abs($s[2] - $db) -gt 16) { $desvio++ }
    }
    if ($amostrados -gt 0) { $fracDesvio = [Math]::Round(100.0 * $desvio / $amostrados, 3) }
    Write-Output "cor dominante: rgb($dominante) em $melhorN de $amostrados amostras"
    Write-Output "pixels fora  : $desvio de $amostrados nao sao da cor dominante ($fracDesvio%)"
} catch {
    Write-Output "pixels       : nao medidos (System.Drawing indisponivel: $($_.Exception.Message))"
}
# tamanho definitivo, lido depois que o arquivo parou de ser reescrito
$pngFinal = (Get-Item -LiteralPath $PngAbs).Length
Write-Output "png          : $pngFinal bytes"
if ($errTxt) {
    $linhas = ($errTxt -split '\r?\n' | Where-Object { $_ -match '\S' } | Select-Object -First 6)
    foreach ($l in $linhas) { Write-Output "stderr edge  : $l" }
}

if ($dim -ne $null -and ($dim[0] -lt 200 -or $dim[1] -lt 200)) {
    Write-Output "RENDER VAZIO: PNG de $($dim[0])x$($dim[1]) px, pequeno demais para ser uma pagina."
    exit 2
}
if ($fracDesvio -ne $null -and $fracDesvio -lt 0.5) {
    Write-Output "RENDER VAZIO: a imagem e uma tela chapada (0.5% ou menos dos pixels fogem da cor dominante)."
    if ($ext -eq '.pdf') {
        Write-Output "              Nesta maquina o Edge headless nao desenha o conteudo do PDF:"
        Write-Output "              ele nao carrega a extensao visualizadora. O PNG NAO prova nada sobre o PDF."
        Write-Output "              Controle: rode este mesmo script num .html e compare."
    }
    exit 2
}
Write-Output "RENDER OK: o PNG foi escrito e tem regiao nao-chapada."
exit 0
