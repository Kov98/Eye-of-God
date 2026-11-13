# Requer -Version 5.1
# Fixture da Galeria: tres PNGs reais copiados das evidencias + um arquivo que nao
# e midia. Deterministico: apaga e refaz.
# Uso: powershell -File .oracle_crash\make_gallery_fixture.ps1
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
$dir = Join-Path $here 'galeria_fixture'
if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $dir 'sub') | Out-Null

$origem = Get-ChildItem -Path (Join-Path $root '.oracle_gallery\shots') -Filter *.png -File |
    Sort-Object Name | Select-Object -First 3
if ($origem.Count -lt 3) { throw 'faltam PNGs em .oracle_gallery\shots para a fixture' }
$i = 0
foreach ($f in $origem) {
    ++$i
    $destino = if ($i -eq 3) { Join-Path $dir ('sub\' + $f.Name) } else { Join-Path $dir $f.Name }
    Copy-Item $f.FullName $destino -Force
}
Set-Content -Path (Join-Path $dir 'anotacao.txt') -Value 'nao e midia' -Encoding UTF8
Write-Host ('fixture da galeria: ' + $origem.Count + ' PNG(s) em ' + $dir)
