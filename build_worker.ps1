$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$gpp = 'C:\msys64\ucrt64\bin\g++.exe'
# The worker collects in-process, so it links the collector, transport and
# DPAPI vault directly instead of launching OlhoDeDeus_deep.exe.
$sources = @(
    'cloud/service/worker_main.cpp',
    'cloud/service/job_queue.cpp',
    'cloud/enterprise/secret_store.cpp',
    'cloud/cli/collect_flow.cpp',
    'cloud/connectors/m365/graph_client.cpp',
    'cloud/connectors/m365/m365_collector.cpp',
    'cloud/core/cloud_core.cpp',
    'cloud/core/winhttp_transport.cpp',
    'cloud/core/dpapi_token_vault.cpp'
) | ForEach-Object { Join-Path $root $_ }
$out = Join-Path $root 'cloud_worker.exe'
& $gpp -std=c++17 -O2 -fms-extensions '-Wl,--no-insert-timestamp' -I $root -I (Join-Path $root 'third_party') `
    -o $out @sources -lwinhttp -lcrypt32 -lbcrypt -ladvapi32 -lws2_32 -static
if ($LASTEXITCODE -ne 0) { throw "worker build failed: $LASTEXITCODE" }
Write-Host ("built {0} ({1:n0} bytes)" -f (Split-Path $out -Leaf), (Get-Item $out).Length)
