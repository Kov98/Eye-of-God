#Requires -Version 5.1
# Build de DIAGNOSTICO do GUI: mesmas fontes do produto, com -g para o addr2line
# resolver a pilha do relatorio de crash. Gera OlhoDeDeus_gui_dbg.exe e nao toca
# no binario que o produto usa.
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

$gpp = 'C:\msys64\ucrt64\bin\g++.exe'
if (-not (Test-Path $gpp)) { $gpp = (Get-Command g++ -ErrorAction Stop).Source }
$gcc = Join-Path (Split-Path $gpp -Parent) 'gcc.exe'
if (-not (Test-Path $gcc)) { $gcc = (Get-Command gcc -ErrorAction Stop).Source }

$sqliteSource = Join-Path $root 'third_party\sqlite\sqlite3.c'
$sqliteObject = Join-Path $root 'third_party\sqlite\sqlite3.o'
if (-not (Test-Path $sqliteObject)) {
    & $gcc -O2 -c $sqliteSource -o $sqliteObject
    if ($LASTEXITCODE -ne 0) { throw 'sqlite build failed' }
}

# Mesma lista de fontes do build_deep.ps1 (guiSources). Se divergir, o binario de
# diagnostico deixa de representar o produto.
$sources = @(
    'gui_main.cpp', 'mainwindow.cpp', 'chatview.cpp', 'acquireview.cpp',
    'galleryview.cpp', 'timeline_index.cpp', 'ios_keybag.cpp', 'ios_backup.cpp',
    'android_adb.cpp', 'android_backup.cpp', 'social/zip_reader.cpp',
    'social/mbox_reader.cpp', 'social/takeout_import.cpp', 'social/telegram_import.cpp',
    'social/dyi_import.cpp', 'manifest_sign.cpp', 'hardware_inventory.cpp',
    'cloud/core/cloud_core.cpp', 'cloud/core/oauth_pkce.cpp', 'cloud/core/oauth_callback.cpp',
    'cloud/core/winhttp_transport.cpp', 'cloud/core/oidc.cpp', 'cloud/core/rbac.cpp',
    'cloud/enterprise/identity.cpp', 'cloud/enterprise/session.cpp',
    'cloud/enterprise/session_vault.cpp', 'cloud/enterprise/secret_store.cpp',
    'cloud/cli/auth_flow.cpp', 'cloud/cli/dropbox_auth.cpp', 'cloud/cli/collect_flow.cpp',
    'cloud/cli/dropbox_flow.cpp', 'cloud/connectors/dropbox/dropbox_client.cpp',
    'cloud/connectors/dropbox/dropbox_http.cpp', 'cloud/connectors/dropbox/dropbox_collector.cpp',
    'cloud/connectors/m365/graph_client.cpp', 'cloud/connectors/m365/m365_collector.cpp',
    'cloud/core/dpapi_token_vault.cpp', 'cloud/service/job_queue.cpp',
    'cloud/enterprise/audit.cpp'
) | ForEach-Object { Join-Path $root $_ }

$qtPkg = 'C:\msys64\ucrt64\bin\pkg-config.exe'
if (-not (Test-Path $qtPkg)) { $qtPkg = (Get-Command pkg-config -ErrorAction Stop).Source }
$qtFlags = ((& $qtPkg --cflags --libs Qt6Widgets Qt6Network) -join ' ') -split '\s+' | Where-Object { $_ -ne '' }

$out = Join-Path $root 'OlhoDeDeus_gui_dbg.exe'
$extra = @('-lcomdlg32','-lshell32','-lole32','-lgdi32','-lpsapi','-lbcrypt','-lwinhttp','-lws2_32','-lcrypt32','-ltbs')
$args = @('-std=c++17','-O2','-g','-fms-extensions','-Wl,--no-insert-timestamp','-mwindows','-I',$root,'-I',(Join-Path $root 'third_party\sqlite'),'-o',$out) + $sources + @($sqliteObject) + $qtFlags + $extra
& $gpp @args
if ($LASTEXITCODE -ne 0) { throw "build de diagnostico falhou (exit $LASTEXITCODE)" }
$item = Get-Item $out
Write-Host ("built {0} ({1:n0} bytes, com simbolos)" -f $item.Name, $item.Length)
