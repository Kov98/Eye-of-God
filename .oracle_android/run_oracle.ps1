# language: PowerShell, file: run_oracle.ps1
# Rebuilds every fixture, every binary and every check, and writes RESULTS.log.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .oracle_android\run_oracle.ps1
$ErrorActionPreference = 'Stop'

$oracleDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $oracleDir
$gpp = 'C:\msys64\ucrt64\bin\g++.exe'
$obj = Join-Path $oracleDir 'obj'
$fixtures = Join-Path $oracleDir 'fixtures'
$outDir = Join-Path $oracleDir 'out'
$log = Join-Path $oracleDir 'RESULTS.log'

New-Item -ItemType Directory -Force $obj, $outDir | Out-Null
$transcript = New-Object System.Collections.Generic.List[string]

function Say([string]$text) {
    Write-Host $text
    $transcript.Add($text)
}

function Run([string]$what, [scriptblock]$block) {
    Say ''
    Say "### $what"
    $global:LASTEXITCODE = 0
    $output = & $block 2>&1 | Out-String
    if ($output.Trim().Length -gt 0) {
        foreach ($line in ($output -split "\r?\n")) { if ($line.Trim()) { Say $line } }
    }
    if ($global:LASTEXITCODE -ne 0) {
        Say "FALHOU: $what (exit $global:LASTEXITCODE)"
        throw "$what falhou"
    }
}

Push-Location $repo
try {
    Say 'Olho de Deus - oraculo do modulo Android'
    Say ('gerado em ' + (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))
    Say ('compilador: ' + (& $gpp --version | Select-Object -First 1))
    Say ('fixtures: ' + $fixtures)

    Run 'gerar fixtures (.ab cifrados + amostras adb) com o oraculo Python/.NET' {
        python (Join-Path $oracleDir 'gen_ab.py')
    }

    # -Werror turns "compila limpo com -Wall -Wextra" into a check, not a claim.
    Run 'compilar android_adb.cpp (-Wall -Wextra -Werror)' {
        & $gpp -std=c++17 -O2 -Wall -Wextra -Werror -I . -I third_party/sqlite -c android_adb.cpp -o (Join-Path $obj 'android_adb.o')
    }
    Run 'compilar android_backup.cpp (-Wall -Wextra -Werror)' {
        & $gpp -std=c++17 -O2 -Wall -Wextra -Werror -I . -I third_party/sqlite -c android_backup.cpp -o (Join-Path $obj 'android_backup.o')
    }
    Run 'compilar fake_adb.exe (adb de mentira para a camada de processo)' {
        & $gpp -std=c++17 -O2 -Wall -Wextra -Werror -I . -c (Join-Path $oracleDir 'harness\fake_adb.cpp') -o (Join-Path $obj 'fake_adb_main.o')
        & $gpp -municode (Join-Path $obj 'fake_adb_main.o') -o (Join-Path $obj 'fake_adb.exe') -lbcrypt -lws2_32
    }
    Run 'linkar o oraculo junto de third_party/sqlite/sqlite3.o (prova de ausencia de colisao de simbolos)' {
        $linkArgs = @(
            '-municode', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', '.', '-I', 'third_party/sqlite',
            (Join-Path $oracleDir 'harness\oracle.cpp'),
            (Join-Path $obj 'android_adb.o'),
            (Join-Path $obj 'android_backup.o'),
            'third_party/sqlite/sqlite3.o',
            '-o', (Join-Path $obj 'oracle.exe'),
            '-lbcrypt', '-lws2_32'
        )
        & $gpp @linkArgs
    }

    if (Test-Path $outDir) {
        Get-ChildItem -Recurse -Force $outDir | ForEach-Object { try { $_.Attributes = 'Normal' } catch {} }
        Remove-Item -Recurse -Force $outDir -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Force $outDir | Out-Null

    Run 'executar as verificacoes' {
        & (Join-Path $obj 'oracle.exe') $fixtures $outDir (Join-Path $obj 'fake_adb.exe')
    }
} finally {
    Pop-Location
    $transcript | Set-Content -Path $log -Encoding utf8
    Write-Host ''
    Write-Host "log: $log"
}
