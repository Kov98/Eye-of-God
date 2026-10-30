#Requires -Version 5.1
# Build OlhoDeDeus_deep.exe from the collector + deep engine + artifact modules.
# The original OlhoDeDeus.exe is never touched.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

$gpp = $null
$gcc = $null
foreach ($candidate in @(
    'C:\msys64\ucrt64\bin\g++.exe',
    'C:\msys64\mingw64\bin\g++.exe'
)) {
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
    'case_db.cpp',
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
    'cloud/cli/auth_flow.cpp',
    'cloud/cli/collect_flow.cpp',
    'gui.cpp'
) | ForEach-Object { Join-Path $root $_ }
$libs = @(
    '-lws2_32', '-liphlpapi', '-ladvapi32', '-lwevtapi', '-lbcrypt',
    '-lshell32', '-lole32', '-loleaut32', '-lshlwapi', '-lxmllite',
    '-luuid', '-lesent', '-lcrypt32', '-lcomdlg32', '-lgdi32', '-lwinhttp', '-static'
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
    (Join-Path $root 'manifest_sign.cpp'),
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
    (Join-Path $root 'cloud/core/dpapi_token_vault.cpp'),
    (Join-Path $root 'cloud/service/job_queue.cpp'),
    (Join-Path $root 'cloud/enterprise/audit.cpp')
)
$qtPkg = 'C:\msys64\ucrt64\bin\pkg-config.exe'
$qtFlags = ((& $qtPkg --cflags --libs Qt6Widgets Qt6Network) -join ' ') -split '\s+'
& $gpp -std=c++17 -O2 -fms-extensions '-Wl,--no-insert-timestamp' -mwindows -I $root `
    -o $guiOut @guiSources @qtFlags -lcomdlg32 -lshell32 -lole32 -lgdi32 -lpsapi -lbcrypt -lwinhttp -lws2_32 -lcrypt32
if ($LASTEXITCODE -ne 0) { throw "GUI build failed with exit code $LASTEXITCODE" }# Keep VirtualBox UI icons next to the GUI for QIcon.
$iconSource = Join-Path $root 'resources\vbox_icons'
$iconTarget = Join-Path $root 'icons'
if (Test-Path $iconSource) {
    New-Item -ItemType Directory -Force -Path $iconTarget | Out-Null
    Copy-Item (Join-Path $iconSource '*.png') $iconTarget -Force
}
$guiItem = Get-Item $guiOut
Write-Host ("built {0} ({1:n0} bytes)" -f $guiItem.Name, $guiItem.Length)
