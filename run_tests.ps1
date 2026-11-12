#Requires -Version 5.1
param([switch]$Full)
# Suite unica do Olho de Deus.
#
# -Full acrescenta o build do produto (build_deep.ps1). Fica fora do padrao
# porque reconstruir os cinco binarios custa minutos; sem ele a suite prova os
# modulos mas nao prova que o executavel principal linka.
#
# Roda tudo: os testes unitarios desta sessao, as suites dos modulos, e o ciclo
# ponta a ponta (gerar laudo, assinar, carimbar, adulterar). Sai 0 so quando tudo
# passa; imprime um placar unico no fim.
$ErrorActionPreference = "Continue"
$PSNativeCommandUseErrorActionPreference = $false
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
# Compilador usado pelas etapas que constroem o proprio binario. Definido aqui
# porque a primeira dessas etapas roda antes da secao 3.
$gpp = "C:\msys64\ucrt64\bin\g++.exe"
Set-Location $root

$results = @()
function Stage($name, $ok, $detail) {
    $script:results += [pscustomobject]@{ Name = $name; Ok = $ok; Detail = $detail }
    $tag = if ($ok) { "PASS" } else { "FAIL" }
    Write-Host ("[{0}] {1,-34} {2}" -f $tag, $name, $detail)
}

function Run-Exe($name, $path, $argsList) {
    if (-not (Test-Path $path)) { Stage $name $false "binario ausente: $path"; return }
    $out = & $path @argsList 2>&1
    $code = $LASTEXITCODE
    $tail = ($out | Where-Object { $_.ToString().Trim() -ne "" } | Select-Object -Last 1)
    Stage $name ($code -eq 0) ("exit=" + $code + "  " + $tail)
}

Write-Host ""
Write-Host "=== 1. cripto e recuperacao ===" -ForegroundColor Cyan
Run-Exe "SQLCipher round-trip" ".oracle\watest.exe" @(".oracle\msgstore2.db", ".oracle\rt.db", "AA6323D8738EEDF583777736D53F842A9D4409E3BF39A3C073EBDB72AEDBFB40")
# Midia do WhatsApp: descoberta no banco decifrado, decifra dos .enc e a marca
# de "ver uma vez". O binario e reconstruido aqui porque rodar um mediatest.exe
# velho deixaria a etapa verde mesmo com o modulo quebrado.
$waMediaExe = ".oracle\wa_media_test.exe"
Remove-Item $waMediaExe -ErrorAction SilentlyContinue
$sqliteForWa = "third_party\sqlite\sqlite3.o"
if (-not (Test-Path $sqliteForWa)) {
    Stage "WhatsApp: midia + ver-uma-vez" $false "sqlite3.o ausente: rode build_deep.ps1"
} else {
    & $gpp -std=c++17 -O2 -Wall -Wextra -I . -I third_party -I third_party/sqlite ".oracle\wa_media_test.cpp" "wa_media.cpp" $sqliteForWa -lbcrypt -o $waMediaExe 2>&1 | Out-Null
    Run-Exe "WhatsApp: midia + ver-uma-vez" $waMediaExe @(".oracle")
}
Run-Exe "carver de apagadas" ".oracle\carvetest.exe" @()

# Selo de integridade do banco do caso: db + journal + WAL, HMAC-SHA256 por bloco.
Remove-Item ".oracle\seal_test.exe" -ErrorAction SilentlyContinue
Remove-Item ".oracle\seal_case", ".oracle\seal_set" -Recurse -Force -ErrorAction SilentlyContinue
& $gpp -std=c++17 -O2 -Wall -Wextra -I . ".oracle\seal_test.cpp" "case_db_seal.cpp" "case_crypto.cpp" -lbcrypt -lcrypt32 -o ".oracle\seal_test.exe" 2>&1 | Out-Null
Run-Exe "selo do caso (HMAC por bloco)" ".oracle\seal_test.exe" @()

# E o mesmo caminho pelo executavel do produto, que e o que a GUI chama.
$sealDir = ".oracle\seal_suite"
Remove-Item $sealDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $sealDir | Out-Null
[System.IO.File]::WriteAllBytes((Join-Path $sealDir 'case.db'), (New-Object byte[] 8192))
& ".\OlhoDeDeus_deep.exe" --case-seal --case $sealDir 2>&1 | Out-Null
$sealOk = ($LASTEXITCODE -eq 0)
& ".\OlhoDeDeus_deep.exe" --case-check --case $sealDir 2>&1 | Out-Null
$checkOk = ($LASTEXITCODE -eq 0)
$tampered = [System.IO.File]::ReadAllBytes((Join-Path $sealDir 'case.db'))
$tampered[10] = $tampered[10] -bxor 0x01
[System.IO.File]::WriteAllBytes((Join-Path $sealDir 'case.db'), $tampered)
& ".\OlhoDeDeus_deep.exe" --case-check --case $sealDir 2>&1 | Out-Null
$tamperExit = $LASTEXITCODE
Stage "selo: produto sela e detecta" ($sealOk -and $checkOk -and ($tamperExit -eq 5)) ("sela=" + $sealOk + " verifica=" + $checkOk + " adulterado_exit=" + $tamperExit)

# Selo da midia extraida, pelo executavel do produto: o que a recuperacao
# escreve (report.json + media/) entra no selo. Adulterar um JPEG, plantar
# arquivo novo ou apagar tem que reprovar com exit 5 e apontar o arquivo.
$mediaCase = ".oracle\media_seal_suite"
Remove-Item $mediaCase -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $mediaCase "recovered\media") | Out-Null
[System.IO.File]::WriteAllBytes((Join-Path $mediaCase "case.db"), (New-Object byte[] 4096))
[System.IO.File]::WriteAllBytes((Join-Path $mediaCase "recovered\report.json"), [System.Text.Encoding]::UTF8.GetBytes('{"midia": []}'))
$fotoMidia = Join-Path $mediaCase "recovered\media\1_foto.jpg"
[System.IO.File]::WriteAllBytes($fotoMidia, (New-Object byte[] 5000))
[System.IO.File]::WriteAllBytes((Join-Path $mediaCase "recovered\media\2_video.mp4"), (New-Object byte[] 70000))
& ".\OlhoDeDeus_deep.exe" --case-seal --case $mediaCase 2>&1 | Out-Null
$mediaSealed = ($LASTEXITCODE -eq 0)
$mediaCheck = (& ".\OlhoDeDeus_deep.exe" --case-check --case $mediaCase 2>&1) -join " "
$mediaIntact = ($LASTEXITCODE -eq 0) -and ($mediaCheck -match "midia-check: ok") -and ($mediaCheck -match "arquivos: 3")
$mediaBytes = [System.IO.File]::ReadAllBytes($fotoMidia)
$mediaBytes[1234] = $mediaBytes[1234] -bxor 0x01
[System.IO.File]::WriteAllBytes($fotoMidia, $mediaBytes)
$mediaTampered = (& ".\OlhoDeDeus_deep.exe" --case-check --case $mediaCase 2>&1) -join " "
$mediaTamperExit = $LASTEXITCODE
$mediaBytes[1234] = $mediaBytes[1234] -bxor 0x01
[System.IO.File]::WriteAllBytes($fotoMidia, $mediaBytes)
$intruso = Join-Path $mediaCase "recovered\media\3_intruso.jpg"
[System.IO.File]::WriteAllBytes($intruso, (New-Object byte[] 100))
$mediaAdded = (& ".\OlhoDeDeus_deep.exe" --case-check --case $mediaCase 2>&1) -join " "
$mediaAddedExit = $LASTEXITCODE
Remove-Item $intruso -Force
& ".\OlhoDeDeus_deep.exe" --case-check --case $mediaCase 2>&1 | Out-Null
$mediaBackOk = ($LASTEXITCODE -eq 0)
Stage "selo: midia extraida" ($mediaSealed -and $mediaIntact -and $mediaBackOk -and ($mediaTamperExit -eq 5) -and ($mediaTampered -match "1_foto.jpg") -and ($mediaAddedExit -eq 5) -and ($mediaAdded -match "3_intruso.jpg")) ("sela=" + $mediaSealed + " intacto=" + $mediaIntact + " adulterado_exit=" + $mediaTamperExit + " intruso_exit=" + $mediaAddedExit)

# Selagem longa tem que avisar progresso (silencio de minutos parece travamento)
# e a publicacao do selo tem que ser atomica: se a re-selagem nao consegue
# publicar, o selo anterior tem que sobreviver inteiro.
$progCase = ".oracle\seal_progresso_suite"
Remove-Item $progCase -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $progCase "recovered\media") | Out-Null
1..40 | ForEach-Object { [System.IO.File]::WriteAllBytes((Join-Path $progCase "recovered\media\f$_.jpg"), (New-Object byte[] (400 + $_))) }
# Um arquivo grande de proposito: o progresso tem que mostrar BYTES, senao um
# video de 8 GB ficaria parado em "1/1" e o operador acharia que travou.
[System.IO.File]::WriteAllBytes((Join-Path $progCase "recovered\media\grande.bin"), (New-Object byte[] 8388608))
# Banco do caso: o selo dele e streaming e conta BLOCOS, nao arquivos.
[System.IO.File]::WriteAllBytes((Join-Path $progCase "case.db"), (New-Object byte[] 8192))
$progOut = (& ".\OlhoDeDeus_deep.exe" --case-seal --case $progCase 2>&1) -join " "
$progSealed = ($LASTEXITCODE -eq 0) -and ($progOut -match "\.\.\. \d+/\d+ arquivos")
$byteShown = $progOut -match "\d+\.\d+ MB"
$blockShown = $progOut -match "\d+/\d+ blocos"
$noTmp = (@(Get-ChildItem $progCase -Recurse -Filter *.tmp).Count -eq 0)
$lockExit = -1
$survived = $false
try {
    $lock = [System.IO.File]::Open((Join-Path $progCase "recovered\media.seal"), 'Open', 'ReadWrite', 'None')
    & ".\OlhoDeDeus_deep.exe" --case-seal --case $progCase 2>&1 | Out-Null
    $lockExit = $LASTEXITCODE
    $lock.Close()
} catch { $lockExit = -1 }
& ".\OlhoDeDeus_deep.exe" --case-check --case $progCase 2>&1 | Out-Null
$survived = ($LASTEXITCODE -eq 0)
$stillNoTmp = (@(Get-ChildItem $progCase -Recurse -Filter *.tmp).Count -eq 0)
Stage "selo: progresso + escrita atomica" ($progSealed -and $byteShown -and $blockShown -and $noTmp -and ($lockExit -eq 5) -and $survived -and $stillNoTmp) ("progresso=" + $progSealed + " bytes=" + $byteShown + " blocos=" + $blockShown + " sem_tmp=" + $noTmp + " travado_exit=" + $lockExit + " selo_sobreviveu=" + $survived)

# Aquisicao de nuvem e export social tambem sao conteudo extraido do caso:
# entram no mesmo selo, pasta por pasta, com o mesmo veredito.
New-Item -ItemType Directory -Force (Join-Path $mediaCase "cloud\dropbox\conta@exemplo") | Out-Null
[System.IO.File]::WriteAllBytes((Join-Path $mediaCase "cloud\dropbox\conta@exemplo\manifest.json"), [System.Text.Encoding]::UTF8.GetBytes('{"provider": "dropbox"}'))
$cloudArquivo = Join-Path $mediaCase "cloud\dropbox\conta@exemplo\arquivo.bin"
[System.IO.File]::WriteAllBytes($cloudArquivo, (New-Object byte[] 3000))
& ".\OlhoDeDeus_deep.exe" --case-seal --case $mediaCase 2>&1 | Out-Null
$cloudSealed = ($LASTEXITCODE -eq 0) -and (Test-Path (Join-Path $mediaCase "recovered\media.seal")) -and (Test-Path (Join-Path $mediaCase "cloud\media.seal"))
$cloudCheck = (& ".\OlhoDeDeus_deep.exe" --case-check --case $mediaCase 2>&1) -join " "
$cloudIntact = ($LASTEXITCODE -eq 0) -and ($cloudCheck -match "cloud") -and ($cloudCheck -match "arquivos: 2")
$cloudBytes = [System.IO.File]::ReadAllBytes($cloudArquivo)
$cloudBytes[500] = $cloudBytes[500] -bxor 0x01
[System.IO.File]::WriteAllBytes($cloudArquivo, $cloudBytes)
$cloudTampered = (& ".\OlhoDeDeus_deep.exe" --case-check --case $mediaCase 2>&1) -join " "
$cloudTamperExit = $LASTEXITCODE
$cloudBytes[500] = $cloudBytes[500] -bxor 0x01
[System.IO.File]::WriteAllBytes($cloudArquivo, $cloudBytes)
& ".\OlhoDeDeus_deep.exe" --case-check --case $mediaCase 2>&1 | Out-Null
$cloudBackOk = ($LASTEXITCODE -eq 0)
Stage "selo: nuvem e social" ($cloudSealed -and $cloudIntact -and $cloudBackOk -and ($cloudTamperExit -eq 5) -and ($cloudTampered -match "cloud")) ("sela=" + $cloudSealed + " intacto=" + $cloudIntact + " adulterado_exit=" + $cloudTamperExit)
# Indice do caso: sem ele, apagar a pasta leva o selo junto e o caso parece limpo.
$indexPresent = (Test-Path (Join-Path $mediaCase "case.seal")) -and ($cloudCheck -match "indice-check: ok")
Move-Item (Join-Path $mediaCase "cloud") (Join-Path $mediaCase "cloud_gone") -Force
$indexGone = (& ".\OlhoDeDeus_deep.exe" --case-check --case $mediaCase 2>&1) -join " "
$indexGoneExit = $LASTEXITCODE
Move-Item (Join-Path $mediaCase "cloud_gone") (Join-Path $mediaCase "cloud") -Force
& ".\OlhoDeDeus_deep.exe" --case-check --case $mediaCase 2>&1 | Out-Null
$indexBackOk = ($LASTEXITCODE -eq 0)

# Caso so de recuperacao (whatsapp/ + recovered/, sem o SQLite do caso) tambem
# tem que selar: e o caso que a GUI monta no fim de uma aquisicao de WhatsApp.
$woCase = ".oracle\seal_sem_db"
Remove-Item $woCase -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $woCase "recovered\media"), (Join-Path $woCase "whatsapp") | Out-Null
[System.IO.File]::WriteAllBytes((Join-Path $woCase "whatsapp\msgstore.db"), (New-Object byte[] 4096))
[System.IO.File]::WriteAllBytes((Join-Path $woCase "recovered\media\1.jpg"), (New-Object byte[] 2000))
$woOut = (& ".\OlhoDeDeus_deep.exe" --case-seal --case $woCase 2>&1) -join " "
$woSealed = ($LASTEXITCODE -eq 0) -and ($woOut -match "sem case.db") -and (Test-Path (Join-Path $woCase "case.seal"))
& ".\OlhoDeDeus_deep.exe" --case-check --case $woCase 2>&1 | Out-Null
$woOk = ($LASTEXITCODE -eq 0)
Stage "selo: caso sem case.db" ($woSealed -and $woOk) ("sela=" + $woSealed + " verifica=" + $woOk)
Stage "selo: indice do caso" ($indexPresent -and ($indexGoneExit -eq 5) -and ($indexGone -match "cloud/media.seal") -and $indexBackOk) ("indice=" + $indexPresent + " pasta_removida_exit=" + $indexGoneExit + " volta=" + $indexBackOk)

# O laudo confere o selo ANTES de gerar: caso adulterado nao vira PDF.
# Tres estados: intacto gera, adulterado recusa (exit 4, nada escrito) e
# --allow-broken-seal emite o laudo documentando a violacao.
function Pdf-Has([string]$path, [string]$needle) {
    if (-not (Test-Path $path)) { return $false }
    $raw = [System.Text.Encoding]::GetEncoding(28591).GetString([System.IO.File]::ReadAllBytes($path))
    return $raw.Contains($needle)
}
$gateCase = ".oracle\seal_gate_suite"
Remove-Item $gateCase -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $gateCase "recovered\media") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $gateCase "cloud\dropbox\conta") | Out-Null
[System.IO.File]::WriteAllBytes((Join-Path $gateCase "case.db"), (New-Object byte[] 4096))
$gateCloud = Join-Path $gateCase "cloud\dropbox\conta\manifest.json"
[System.IO.File]::WriteAllBytes($gateCloud, [System.Text.Encoding]::UTF8.GetBytes('{"provider": "dropbox"}'))
$gateFoto = Join-Path $gateCase "recovered\media\1_foto.jpg"
[System.IO.File]::WriteAllBytes($gateFoto, (New-Object byte[] 5000))
& ".\OlhoDeDeus_deep.exe" --case-seal --case $gateCase 2>&1 | Out-Null
$gateSealed = ($LASTEXITCODE -eq 0)
$gateOkPdf = ".oracle\gate_ok.pdf"
Remove-Item $gateOkPdf -Force -ErrorAction SilentlyContinue
& ".\olho_report.exe" (Join-Path $gateCase "recovered") --out $gateOkPdf --id "GATE-1" --tsa-selftest 2>&1 | Out-Null
$gateOk = ($LASTEXITCODE -eq 0) -and (Pdf-Has $gateOkPdf "Selo de integridade: VERIFICADO")
$gateBytes = [System.IO.File]::ReadAllBytes($gateFoto)
$gateBytes[100] = $gateBytes[100] -bxor 0x01
[System.IO.File]::WriteAllBytes($gateFoto, $gateBytes)
$gateBadPdf = ".oracle\gate_bad.pdf"
Remove-Item $gateBadPdf -Force -ErrorAction SilentlyContinue
$gateMsg = (& ".\olho_report.exe" (Join-Path $gateCase "recovered") --out $gateBadPdf --id "GATE-2" 2>&1) -join " "
$gateBadExit = $LASTEXITCODE
$gateRefused = ($gateBadExit -eq 4) -and (-not (Test-Path $gateBadPdf)) -and ($gateMsg -match "1_foto.jpg") -and ($gateMsg -match "RECUSADO")
$gateAllowedPdf = ".oracle\gate_allowed.pdf"
Remove-Item $gateAllowedPdf -Force -ErrorAction SilentlyContinue
& ".\olho_report.exe" (Join-Path $gateCase "recovered") --out $gateAllowedPdf --id "GATE-3" --allow-broken-seal 2>&1 | Out-Null
$gateAllowed = ($LASTEXITCODE -eq 0) -and (Pdf-Has $gateAllowedPdf "SELO DE INTEGRIDADE VIOLADO")
# Falha SO na pasta de nuvem: o laudo tem que recusar do mesmo jeito.
$gateBytes[100] = $gateBytes[100] -bxor 0x01
[System.IO.File]::WriteAllBytes($gateFoto, $gateBytes)
$gateCloudBytes = [System.IO.File]::ReadAllBytes($gateCloud)
$gateCloudBytes[5] = $gateCloudBytes[5] -bxor 0x01
[System.IO.File]::WriteAllBytes($gateCloud, $gateCloudBytes)
$gateCloudPdf = ".oracle\gate_cloud.pdf"
Remove-Item $gateCloudPdf -Force -ErrorAction SilentlyContinue
$gateCloudMsg = (& ".\olho_report.exe" (Join-Path $gateCase "recovered") --out $gateCloudPdf --id "GATE-4" 2>&1) -join " "
$gateCloudExit = $LASTEXITCODE
$gateCloudRefused = ($gateCloudExit -eq 4) -and (-not (Test-Path $gateCloudPdf)) -and ($gateCloudMsg -match "cloud")
# Selo apagado deixou de ser ambiguo: o indice do caso registra cada selo, entao
# apagar um deles e adulteracao e o laudo RECUSA (antes era so um aviso).
$gateCloudBytes[5] = $gateCloudBytes[5] -bxor 0x01
[System.IO.File]::WriteAllBytes($gateCloud, $gateCloudBytes)
$gateCloudSeal = Join-Path $gateCase "cloud\media.seal"
Copy-Item $gateCloudSeal ".oracle\gate_cloud_seal.bin" -Force
Remove-Item $gateCloudSeal -Force
$gateSealGonePdf = ".oracle\gate_sem_selo.pdf"
Remove-Item $gateSealGonePdf -Force -ErrorAction SilentlyContinue
$gateSealGoneMsg = (& ".\olho_report.exe" (Join-Path $gateCase "recovered") --out $gateSealGonePdf --id "GATE-5" 2>&1) -join " "
$gateSealGoneExit = $LASTEXITCODE
$gateSealGoneRefused = ($gateSealGoneExit -eq 4) -and (-not (Test-Path $gateSealGonePdf)) -and ($gateSealGoneMsg -match "indice")
Copy-Item ".oracle\gate_cloud_seal.bin" $gateCloudSeal -Force
Remove-Item ".oracle\gate_final.pdf" -Force -ErrorAction SilentlyContinue
& ".\olho_report.exe" (Join-Path $gateCase "recovered") --out ".oracle\gate_final.pdf" --id "GATE-6" 2>&1 | Out-Null
$gateFinalOk = ($LASTEXITCODE -eq 0)
Stage "laudo recusa selo violado" ($gateSealed -and $gateOk -and $gateRefused -and $gateAllowed -and $gateCloudRefused -and $gateSealGoneRefused -and $gateFinalOk) ("sela=" + $gateSealed + " intacto_gera=" + $gateOk + " adulterado_exit=" + $gateBadExit + " nuvem_exit=" + $gateCloudExit + " selo_apagado_exit=" + $gateSealGoneExit + " volta=" + $gateFinalOk + " documentado=" + $gateAllowed)



# Reconstrucao do estado atual a partir do par db + wal. Fixture gerada pelo
# sqlite3 do Python (com a conexao aberta, para o WAL nao ser checkpointado).
$sqliteObjWal = "third_party\sqlite\sqlite3.o"
if (Test-Path $sqliteObjWal) {
    python ".oracle\make_wal.py" 2>&1 | Out-Null
    Remove-Item ".oracle\wal_test.exe" -ErrorAction SilentlyContinue
    & $gpp -std=c++17 -O2 -Wall -Wextra -I . -I third_party -I third_party/sqlite ".oracle\wal_test.cpp" "wa_wal.cpp" $sqliteObjWal -o ".oracle\wal_test.exe" 2>&1 | Out-Null
    Run-Exe "WAL: reconstrucao do estado" ".oracle\wal_test.exe" @()
} else {
    Stage "WAL: reconstrucao do estado" $false "sqlite3.o ausente: rode build_cloud.ps1"
}

Write-Host ""
Write-Host "=== 2. assinatura, carimbo e custodia ===" -ForegroundColor Cyan
Run-Exe "CMS PKCS#7" ".oracle\cms_test.exe" @()
Run-Exe "carimbo RFC 3161" ".oracle\tsa_test.exe" @()
Run-Exe "cadeia de custodia" ".oracle\custody_test.exe" @()
Remove-Item ".oracle\case_enc.*", ".oracle\case_db_enc.*" -Force -ErrorAction SilentlyContinue
Run-Exe "cifra do caso (CTR)" ".oracle\ctr_test.exe" @()
Run-Exe "Case DB cifrado" ".oracle\case_db_enc_test.exe" @()
# extract_ab recusa destino existente (nao sobrescreve evidencia), entao a saida
# de execucoes anteriores precisa sair antes — senao a etapa roda zero trabalho.
Remove-Item ".oracle\ab_*" -Recurse -Force -ErrorAction SilentlyContinue
Run-Exe "concorrencia (8 threads)" ".oracle\threads_test.exe" @()
Run-Exe "usbmux (protocolo iOS)" ".oracle\usbmux_test.exe" @()
Run-Exe "lockdown (protocolo iOS)" ".oracle\lockdown_test.exe" @()
Run-Exe "TLS Schannel (loopback)" ".oracle\tls_loopback_test.exe" @()
Run-Exe "AFC (arquivos iOS)" ".oracle\afc_test.exe" @()
Run-Exe "memoria de processo" ".oracle\mem_test.exe" @()
Run-Exe "USB: enumeracao + handles" ".oracle\usb_handle_test.exe" @()

Write-Host ""
Write-Host "=== 3. conector de nuvem (Dropbox) ===" -ForegroundColor Cyan
# Os binarios sao reconstruidos aqui para a etapa nunca depender de um exe velho.
$gpp = "C:\msys64\ucrt64\bin\g++.exe"
Remove-Item ".oracle\dropbox_test.exe", ".oracle\dropbox_collect_test.exe" -ErrorAction SilentlyContinue
& $gpp -std=c++17 -O2 -Wall -Wextra -I . -I third_party -I cloud/connectors/dropbox ".oracle\dropbox_test.cpp" "cloud/connectors/dropbox/dropbox_client.cpp" -o ".oracle\dropbox_test.exe" 2>&1 | Out-Null
Run-Exe "Dropbox: OAuth+PKCE+listagem" ".oracle\dropbox_test.exe" @()

& $gpp -std=c++17 -O2 -Wall -Wextra -I . -I third_party ".oracle\dropbox_collect_test.cpp" "cloud/connectors/dropbox/dropbox_client.cpp" "cloud/connectors/dropbox/dropbox_http.cpp" "cloud/connectors/dropbox/dropbox_collector.cpp" "cloud/core/cloud_core.cpp" -lbcrypt -o ".oracle\dropbox_collect_test.exe" 2>&1 | Out-Null
Run-Exe "Dropbox: coleta ponta a ponta" ".oracle\dropbox_collect_test.exe" @()

Remove-Item ".oracle\dropbox_flow_test.exe" -ErrorAction SilentlyContinue
& $gpp -std=c++17 -O2 -Wall -Wextra -I . -I third_party ".oracle\dropbox_flow_test.cpp" "cloud/cli/dropbox_flow.cpp" "cloud/connectors/dropbox/dropbox_client.cpp" "cloud/connectors/dropbox/dropbox_http.cpp" "cloud/connectors/dropbox/dropbox_collector.cpp" "cloud/core/cloud_core.cpp" -lbcrypt -o ".oracle\dropbox_flow_test.exe" 2>&1 | Out-Null
Run-Exe "Dropbox: fluxo e codigos de saida" ".oracle\dropbox_flow_test.exe" @()

# Importacao do manifesto para o banco do caso. Depende do sqlite3.o que o
# build_cloud.ps1 compila; sem ele a etapa falha com motivo claro.
$sqliteObj = "third_party\sqlite\sqlite3.o"
if (-not (Test-Path $sqliteObj)) {
    Stage "Importacao para o caso" $false "sqlite3.o ausente: rode build_cloud.ps1"
} else {
    Remove-Item ".oracle\cloud_db_import_test.exe" -ErrorAction SilentlyContinue
    & $gpp -std=c++17 -O2 -fms-extensions -I . -I third_party -I third_party/sqlite "cloud\tests\cloud_db_import_test.cpp" "cloud\db\cloud_db_import.cpp" "case_db.cpp" "case_crypto.cpp" "sqlite_enc_vfs.cpp" "cloud\core\cloud_core.cpp" $sqliteObj -lbcrypt -lcrypt32 -o ".oracle\cloud_db_import_test.exe" 2>&1 | Out-Null
    Run-Exe "Importacao para o caso" ".oracle\cloud_db_import_test.exe" @()

    # Exportacao da timeline (CSV/JSON) a partir do banco do caso.
    Remove-Item ".oracle\timeline_export_test.exe" -ErrorAction SilentlyContinue
    Remove-Item ".oracle\tlx_case" -Recurse -Force -ErrorAction SilentlyContinue
    & $gpp -std=c++17 -O2 -fms-extensions -I . -I third_party -I third_party/sqlite ".oracle\timeline_export_test.cpp" "case_timeline_export.cpp" "cloud\db\cloud_db_import.cpp" "case_db.cpp" "case_crypto.cpp" "sqlite_enc_vfs.cpp" "cloud\core\cloud_core.cpp" $sqliteObj -lbcrypt -lcrypt32 -o ".oracle\timeline_export_test.exe" 2>&1 | Out-Null
    Run-Exe "Timeline CSV/JSON" ".oracle\timeline_export_test.exe" @()
}

Write-Host ""
Write-Host "=== 3b. WhatsApp ponta a ponta ===" -ForegroundColor Cyan
# Fixture cifrada pelo .NET -> wa_recover -> report.json. Prova a cadeia toda:
# a marca de midia efemera sai do banco e chega no relatorio que a galeria le.
$out = powershell -NoProfile -ExecutionPolicy Bypass -File ".oracle\wa_recover_e2e.ps1" 2>&1
$code = $LASTEXITCODE
$line = ($out | Select-String -Pattern "wa_e2e:" | Select-Object -Last 1).Line
if (-not $line) { $line = ($out | Where-Object { $_.ToString().Trim() -ne "" } | Select-Object -Last 1) }
Stage "WhatsApp: recuperacao ponta a ponta" ($code -eq 0) ("exit=" + $code + "  " + $line)
if ($code -ne 0) {
    $out | Where-Object { $_ -match "FALHA|ausente|falha" } | ForEach-Object { Write-Host ("    " + $_) -ForegroundColor DarkYellow }
}

Write-Host ""
Write-Host "=== 4. suites dos modulos ===" -ForegroundColor Cyan
foreach ($s in @(@{n="iOS backup+keybag"; p=".oracle_ios\run_all.ps1"}, @{n="Android ADB+.ab"; p=".oracle_android\run_oracle.ps1"}, @{n="Galeria+EXIF"; p=".oracle_gallery\run_all.ps1"}, @{n="Timeline do caso"; p=".oracle_timeline\run_all.ps1"}, @{n="Rust formatguard"; p=".oracle_rust\run.ps1"}, @{n="Export social (DYI)"; p=".oracle_social\run_all.ps1"})) {
    if (-not (Test-Path $s.p)) { Stage $s.n $false "runner ausente"; continue }
    $out = powershell -NoProfile -ExecutionPolicy Bypass -File $s.p 2>&1
    $code = $LASTEXITCODE
    $line = ($out | Select-String -Pattern "checks:|verificacoes:|falharam" | Select-Object -Last 1).Line
    if (-not $line) { $line = ($out | Where-Object { $_.ToString().Trim() -ne "" } | Select-Object -Last 1) }
    Stage $s.n ($code -eq 0) ("exit=" + $code + "  " + ($line -replace "\s+", " ").Trim())
}

$out = python ferramentas\pdfcheck\selftest.py 2>&1
Stage "PDF: validador detecta falha" ($LASTEXITCODE -eq 0) (($out | Select-String "QUEBRAS DETECTADAS" | Select-Object -First 1).Line -replace "\s+", " ")

Write-Host ""
Write-Host "=== 5. ciclo ponta a ponta ===" -ForegroundColor Cyan
$caso = ".oracle\case_test\recovered"
$pdf = ".oracle\suite_laudo.pdf"
Remove-Item $pdf -ErrorAction SilentlyContinue
$out = & ".\olho_report.exe" $caso --out $pdf --id "SUITE-001" --tsa-selftest 2>&1
Stage "gerar laudo (assina+carimba)" ($LASTEXITCODE -eq 0) (($out | Select-String "carimbo de tempo" | Select-Object -First 1).Line -replace "\s+", " ").Trim()

$verify = & ".\olho_verify.exe" $pdf 2>&1
$sigOk = ($LASTEXITCODE -eq 0) -and ($verify -match "ASSINATURA VALIDA")
Stage "verificar assinatura+carimbo" $sigOk (($verify | Select-String "ASSINATURA|carimbo de tempo:" | ForEach-Object { $_.Line.Trim() }) -join " | ")

Copy-Item $pdf ".oracle\suite_mau.pdf" -Force
$b = [System.IO.File]::ReadAllBytes(".oracle\suite_mau.pdf")
$b[6000] = $b[6000] -bxor 0x01
[System.IO.File]::WriteAllBytes(".oracle\suite_mau.pdf", $b)
& ".\olho_verify.exe" ".oracle\suite_mau.pdf" 2>&1 | Out-Null
Stage "adulteracao derruba a assinatura" ($LASTEXITCODE -eq 1) ("exit=" + $LASTEXITCODE)

$conf = python ferramentas\pdfcheck\pdfcheck.py $pdf 2>&1
Stage "conformidade do PDF" ($LASTEXITCODE -eq 0) (($conf | Select-String "RESUMO:" | Select-Object -First 1).Line -replace "\s+", " ")

if ($Full) {
    Write-Host ""
    Write-Host "=== 6. build do produto ===" -ForegroundColor Cyan
    # O linker nao substitui um .exe em execucao: com a GUI aberta o build falha
    # por motivo alheio ao codigo. Dizer isso evita diagnostico errado.
    $running = @(Get-Process OlhoDeDeus_gui, OlhoDeDeus_deep -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) {
        Stage "produto compila e linka" $false ("processo em execucao: feche o app e rode de novo")
    } else {
        $out = powershell -NoProfile -ExecutionPolicy Bypass -File build_deep.ps1 2>&1
        $code = $LASTEXITCODE
        $line = ($out | Select-String -Pattern "built OlhoDeDeus_deep" | Select-Object -First 1).Line
        if (-not $line) { $line = ($out | Select-String -Pattern "error:|undefined" | Select-Object -First 1).Line }
        Stage "produto compila e linka" ($code -eq 0) ([string]$line -replace "\s+", " ").Trim()
    }
}

Write-Host ""
Write-Host "=== PLACAR ===" -ForegroundColor Cyan
$pass = ($results | Where-Object { $_.Ok }).Count
$fail = $results.Count - $pass
foreach ($r in $results | Where-Object { -not $_.Ok }) { Write-Host ("  FALHOU: " + $r.Name + "  " + $r.Detail) -ForegroundColor Red }
Write-Host ("ETAPAS: " + $pass + " de " + $results.Count + " passaram, " + $fail + " falharam")
if ($fail -eq 0) { Write-Host "SUITE: VERDE" -ForegroundColor Green } else { Write-Host "SUITE: VERMELHA" -ForegroundColor Red }
exit $(if ($fail -eq 0) { 0 } else { 1 })