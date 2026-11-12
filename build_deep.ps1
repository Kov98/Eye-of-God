#Requires -Version 5.1
# Build OlhoDeDeus_deep.exe from the collector + deep engine + artifact modules.
# The original OlhoDeDeus.exe is never touched.

$ErrorActionPreference = 'Stop'
# g++ escreve warnings no stderr; com 'Stop' o PowerShell transforma isso em
# erro terminante e o script abortava DEPOIS de compilar tudo, deixando o exe
# velho no lugar (build "verde" com binario antigo). O gate real e o exit code.
$PSNativeCommandUseErrorActionPreference = $false
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

$gpp = $null
$gcc = $null
$candidates = @()
if ($env:OLHO_GXX) { $candidates += $env:OLHO_GXX }
$candidates += @(
    'C:\msys64\ucrt64\bin\g++.exe',
    'C:\msys64\mingw64\bin\g++.exe'
)
foreach ($candidate in $candidates) {
    if (Test-Path $candidate) {
        $gpp = $candidate
        $gcc = Join-Path (Split-Path $candidate -Parent) 'gcc.exe'
        break
    }
}
if (-not $gpp) {
    $command = Get-Command g++ -ErrorAction SilentlyContinue
    if ($command) { $gpp = $command.Source }
}
if (-not $gpp) { throw 'g++ not found. Install MSYS2 UCRT64 or put g++ on PATH.' }
if (-not $gcc -or -not (Test-Path $gcc)) {
    $command = Get-Command gcc -ErrorAction SilentlyContinue
    if ($command) { $gcc = $command.Source }
}
if (-not $gcc) { throw 'gcc not found (needed for the SQLite amalgamation).' }

$sqliteSource = Join-Path $root 'third_party\sqlite\sqlite3.c'
$sqliteObject = Join-Path $root 'third_party\sqlite\sqlite3.o'
if (-not (Test-Path $sqliteSource)) { throw "missing $sqliteSource" }
if (-not (Test-Path $sqliteObject) -or
    (Get-Item $sqliteSource).LastWriteTime -gt (Get-Item $sqliteObject).LastWriteTime) {
    Write-Host "sqlite: $sqliteSource"
    & $gcc -O2 -c $sqliteSource -o $sqliteObject
    if ($LASTEXITCODE -ne 0) { throw "sqlite build failed with exit code $LASTEXITCODE" }
}

$out = Join-Path $root 'OlhoDeDeus_deep.exe'
$sources = @(
    'main.cpp',
    'deep.cpp',
    'hardware_inventory.cpp',
    'recovery_index.cpp',
    'case_db.cpp',
    'case_db_seal.cpp',
    'case_timeline_export.cpp',
    'case_crypto.cpp',
    'sqlite_enc_vfs.cpp',
    'android_backup.cpp',
    'social/zip_reader.cpp',
    'social/mbox_reader.cpp',
    'social/takeout_import.cpp',
    'social/telegram_import.cpp',
    'social/dyi_import.cpp',
    'artifact_registry.cpp',
    'artifact_browser.cpp',
    'artifact_browser_secrets.cpp',
    'artifact_evtx.cpp',
    'artifact_recycle.cpp',
    'artifact_lnk.cpp',
    'artifact_jumplist.cpp',
    'artifact_prefetch.cpp',
    'artifact_shellbags.cpp',
    'artifact_registry_hive.cpp',
    'artifact_amcache.cpp',
    'artifact_shimcache.cpp',
    'artifact_srum.cpp',
    'artifact_bam.cpp',
    'artifact_vss.cpp',
    'artifacts_engine.cpp',
    'imaging.cpp',
    'manifest_sign.cpp',
    'aff4.cpp',
    'wipe_hive.cpp',
    'wipe_browser.cpp',
    'sanitize_disk.cpp',
    'kernel_client.cpp',
    'e01.cpp',
    'exposure.cpp',
    'cloud/core/cloud_core.cpp',
    'cloud/core/oauth_pkce.cpp',
    'cloud/core/oauth_callback.cpp',
    'cloud/core/winhttp_transport.cpp',
    'cloud/core/dpapi_token_vault.cpp',
    'cloud/core/manifest_verify.cpp',
    'cloud/db/cloud_db_import.cpp',
    'cloud/report/cloud_report.cpp',
    'cloud/core/rbac.cpp',
    'cloud/enterprise/identity.cpp',
    'cloud/enterprise/session.cpp',
    'cloud/enterprise/session_vault.cpp',
    'cloud/enterprise/secret_store.cpp',
    'cloud/enterprise/audit.cpp',
    'cloud/connectors/m365/graph_client.cpp',
    'cloud/connectors/m365/m365_collector.cpp',
    'cloud/connectors/dropbox/dropbox_client.cpp',
    'cloud/connectors/dropbox/dropbox_http.cpp',
    'cloud/connectors/dropbox/dropbox_collector.cpp',
    'cloud/cli/dropbox_flow.cpp',
    'cloud/cli/auth_flow.cpp',
    'cloud/cli/dropbox_auth.cpp',
    'cloud/cli/collect_flow.cpp',
    'gui.cpp'
) | ForEach-Object { Join-Path $root $_ }
$libs = @(
    '-lws2_32', '-liphlpapi', '-ladvapi32', '-lwevtapi', '-lbcrypt',
    '-lshell32', '-lole32', '-loleaut32', '-lshlwapi', '-lxmllite',
    '-luuid', '-lesent', '-lcrypt32', '-lcomdlg32', '-lgdi32', '-lwinhttp', '-ltbs', '-static'
)

Write-Host "g++: $gpp"
Write-Host "out: $out"
& $gpp -std=c++17 -O2 -fms-extensions '-Wl,--no-insert-timestamp' -I $root -I (Join-Path $root 'third_party\sqlite') `
    -o $out @sources $sqliteObject @libs
if ($LASTEXITCODE -ne 0) { throw "build failed with exit code $LASTEXITCODE" }

$item = Get-Item $out
Write-Host ("built {0} ({1:n0} bytes)" -f $item.Name, $item.Length)

$guiOut = Join-Path $root 'OlhoDeDeus_gui.exe'
$guiSources = @(
    (Join-Path $root 'gui_main.cpp'),
    (Join-Path $root 'mainwindow.cpp'),
    (Join-Path $root 'chatview.cpp'),
    (Join-Path $root 'acquireview.cpp'),
    (Join-Path $root 'galleryview.cpp'),
    (Join-Path $root 'timeline_index.cpp'),
    (Join-Path $root 'ios_keybag.cpp'),
    (Join-Path $root 'ios_backup.cpp'),
    (Join-Path $root 'android_adb.cpp'),
    (Join-Path $root 'android_backup.cpp'),
    (Join-Path $root 'social/zip_reader.cpp'),
    (Join-Path $root 'social/mbox_reader.cpp'),
    (Join-Path $root 'social/takeout_import.cpp'),
    (Join-Path $root 'social/telegram_import.cpp'),
    (Join-Path $root 'social/dyi_import.cpp'),
    (Join-Path $root 'manifest_sign.cpp'),
    (Join-Path $root 'hardware_inventory.cpp'),
    (Join-Path $root 'cloud/core/cloud_core.cpp'),
    (Join-Path $root 'cloud/core/oauth_pkce.cpp'),
    (Join-Path $root 'cloud/core/oauth_callback.cpp'),
    (Join-Path $root 'cloud/core/winhttp_transport.cpp'),
    (Join-Path $root 'cloud/core/oidc.cpp'),
    (Join-Path $root 'cloud/core/rbac.cpp'),
    (Join-Path $root 'cloud/enterprise/identity.cpp'),
    (Join-Path $root 'cloud/enterprise/session.cpp'),
    (Join-Path $root 'cloud/enterprise/session_vault.cpp'),
    (Join-Path $root 'cloud/enterprise/secret_store.cpp'),
    (Join-Path $root 'cloud/cli/auth_flow.cpp'),
    (Join-Path $root 'cloud/cli/dropbox_auth.cpp'),
    (Join-Path $root 'cloud/cli/collect_flow.cpp'),
    (Join-Path $root 'cloud/cli/dropbox_flow.cpp'),
    (Join-Path $root 'cloud/connectors/dropbox/dropbox_client.cpp'),
    (Join-Path $root 'cloud/connectors/dropbox/dropbox_http.cpp'),
    (Join-Path $root 'cloud/connectors/dropbox/dropbox_collector.cpp'),
    (Join-Path $root 'cloud/connectors/m365/graph_client.cpp'),
    (Join-Path $root 'cloud/connectors/m365/m365_collector.cpp'),
    (Join-Path $root 'cloud/core/dpapi_token_vault.cpp'),
    (Join-Path $root 'cloud/service/job_queue.cpp'),
    (Join-Path $root 'cloud/enterprise/audit.cpp')
)
$qtPkg = $env:OLHO_PKGCONFIG
if (-not $qtPkg) { $qtPkg = 'C:\msys64\ucrt64\bin\pkg-config.exe' }
if (-not (Test-Path $qtPkg)) {
    $c = Get-Command pkg-config -ErrorAction SilentlyContinue
    if ($c) { $qtPkg = $c.Source }
}
if (-not (Test-Path $qtPkg)) { throw 'pkg-config not found' }
$qtFlags = ((& $qtPkg --cflags --libs Qt6Widgets Qt6Network) -join ' ') -split '\s+'
& $gpp -std=c++17 -O2 -fms-extensions '-Wl,--no-insert-timestamp' -mwindows -I $root `
    -I (Join-Path $root 'third_party\sqlite') `
    -o $guiOut @guiSources $sqliteObject @qtFlags -lcomdlg32 -lshell32 -lole32 -lgdi32 -lpsapi -lbcrypt -lwinhttp -lws2_32 -lcrypt32 -ltbs
if ($LASTEXITCODE -ne 0) { throw "GUI build failed with exit code $LASTEXITCODE" }
$guiItem = Get-Item $guiOut
Write-Host ("built {0} ({1:n0} bytes)" -f $guiItem.Name, $guiItem.Length)

# Recuperacao de WhatsApp. Binario proprio porque a GUI o invoca como processo e
# porque ele e a unica peca do produto que abre msgstore.db com a chave do
# aparelho; sem alvo aqui ele ficava fora de qualquer build reproduzivel.
$waOut = Join-Path $root 'wa_recover.exe'
$waSources = @(
    (Join-Path $root 'wa_recover.cpp'),
    (Join-Path $root 'wa_sqlcipher.cpp'),
    (Join-Path $root 'wa_media.cpp'),
    (Join-Path $root 'wa_carve.cpp')
)
& $gpp -std=c++17 -O2 '-Wl,--no-insert-timestamp' -I $root `
    -I (Join-Path $root 'third_party\sqlite') `
    -o $waOut @waSources $sqliteObject -lbcrypt
if ($LASTEXITCODE -ne 0) { throw "wa_recover build failed with exit code $LASTEXITCODE" }
$waItem = Get-Item $waOut
Write-Host ("built {0} ({1:n0} bytes)" -f $waItem.Name, $waItem.Length)

# Laudo em PDF. Binary proprio porque o painel o invoca como processo: um gerador
# de relatorio que travaria a UI nao serve, e o formato PDF nao precisa de Qt.
$reportOut = Join-Path $root 'olho_report.exe'
$reportSources = @(
    (Join-Path $root 'report_main.cpp'),
    (Join-Path $root 'forensic_report.cpp'),
    (Join-Path $root 'evidence_verify.cpp'),
    (Join-Path $root 'pkcs7_builder.cpp'),
    (Join-Path $root 'tsa_client.cpp'),
    (Join-Path $root 'custody_chain.cpp'),
    # Selo do caso: o laudo confere db e midia ANTES de gerar o PDF.
    (Join-Path $root 'case_db_seal.cpp'),
    (Join-Path $root 'case_crypto.cpp'),
    (Join-Path $root 'ios_backup.cpp'),
    (Join-Path $root 'ios_keybag.cpp')
)
# Sem -mwindows: e uma ferramenta de linha de comando e precisa do console.
& $gpp -std=c++17 -O2 -fms-extensions '-Wl,--no-insert-timestamp' -I $root `
    -I (Join-Path $root 'third_party') -I (Join-Path $root 'third_party\sqlite') `
    -o $reportOut @reportSources $sqliteObject -lbcrypt -lws2_32 -lcrypt32 -lncrypt -lwinhttp
if ($LASTEXITCODE -ne 0) { throw "report build failed with exit code $LASTEXITCODE" }
$reportItem = Get-Item $reportOut
Write-Host ("built {0} ({1:n0} bytes)" -f $reportItem.Name, $reportItem.Length)

# Verificador de assinatura: quem recebe um laudo precisa poder conferir sem
# confiar em quem emitiu.
$verifyOut = Join-Path $root 'olho_verify.exe'
& $gpp -std=c++17 -O2 '-Wl,--no-insert-timestamp' -I $root -o $verifyOut `
    (Join-Path $root 'verify_pdf_signature.cpp') (Join-Path $root 'tsa_client.cpp') `
    (Join-Path $root 'pkcs7_builder.cpp') -lcrypt32 -lncrypt -lbcrypt -lwinhttp
if ($LASTEXITCODE -ne 0) { throw "verify build failed with exit code $LASTEXITCODE" }
$verifyItem = Get-Item $verifyOut
Write-Host ("built {0} ({1:n0} bytes)" -f $verifyItem.Name, $verifyItem.Length)

# Verificador de tipo: a evidencia entra declarando o que e; este confere.
$evidenceOut = Join-Path $root 'olho_evidence.exe'
& $gpp -std=c++17 -O2 '-Wl,--no-insert-timestamp' -I $root -o $evidenceOut `
    (Join-Path $root 'evidence_main.cpp') (Join-Path $root 'evidence_verify.cpp')
if ($LASTEXITCODE -ne 0) { throw "evidence build failed with exit code $LASTEXITCODE" }
$evidenceItem = Get-Item $evidenceOut
Write-Host ("built {0} ({1:n0} bytes)" -f $evidenceItem.Name, $evidenceItem.Length)
