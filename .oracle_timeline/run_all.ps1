#Requires -Version 5.1
# Oraculo do carregador de timeline. Compila com -Wall -Wextra e falha em warning:
# e o mesmo porte dos outros oraculos de UI deste repositorio.
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
$build = Join-Path $here 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null

$gpp = 'C:\msys64\ucrt64\bin\g++.exe'
if (-not (Test-Path $gpp)) { $gpp = (Get-Command g++ -ErrorAction Stop).Source }
$pkg = 'C:\msys64\ucrt64\bin\pkg-config.exe'
if (-not (Test-Path $pkg)) { $pkg = (Get-Command pkg-config -ErrorAction Stop).Source }

$pkgOut = Join-Path $build 'pkgconfig.txt'
$pkgProc = Start-Process -FilePath $pkg -ArgumentList @('--cflags', '--libs', 'Qt6Core') -NoNewWindow -Wait -PassThru -RedirectStandardOutput $pkgOut
if ($pkgProc.ExitCode -ne 0) { throw 'pkg-config failed for Qt6Core' }
$qtFlags = ((Get-Content -Raw $pkgOut) -split '\s+') | Where-Object { $_ -ne '' }

$exe = Join-Path $build 'timeline_oracle.exe'
$stdout = Join-Path $build 'timeline_oracle.stdout.txt'
$stderr = Join-Path $build 'timeline_oracle.stderr.txt'
$arguments = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-I', $root,
               (Join-Path $here 'timeline_oracle.cpp'),
               (Join-Path $root 'timeline_index.cpp'),
               (Join-Path $root 'timeline_export.cpp'),
               '-o', $exe) + $qtFlags
$proc = Start-Process -FilePath $gpp -ArgumentList $arguments -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$text = ''
if (Test-Path $stdout) { $text += (Get-Content -Raw $stdout) }
if (Test-Path $stderr) { $text += (Get-Content -Raw $stderr) }
Set-Content -Path (Join-Path $build 'build.log') -Value $text
$warnings = @($text -split '\n' | Where-Object { $_ -match 'warning:' }).Count
if ($proc.ExitCode -ne 0) {
    Write-Host $text
    throw ("timeline_oracle build falhou (exit {0})" -f $proc.ExitCode)
}
if ($warnings -ne 0) {
    Write-Host $text
    throw ("timeline_oracle: {0} warning(s) sob -Wall -Wextra" -f $warnings)
}

& $exe
exit $LASTEXITCODE
