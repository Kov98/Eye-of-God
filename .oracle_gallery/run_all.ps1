#Requires -Version 5.1
# Full pipeline: build, render the base JPEGs, write the fixtures (hand-built
# EXIF), run the oracle over them, then verify. One command, reproducible.
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $here 'build'
$base = Join-Path $here 'base'
$fix = Join-Path $here 'fixtures'

# build.ps1 throws on failure, and -ErrorAction Stop propagates it, so there is
# no $LASTEXITCODE to test here (a script call does not set it).
& (Join-Path $here 'build.ps1')

& (Join-Path $build 'gen_base.exe') $base
if ($LASTEXITCODE -ne 0) { throw 'gen_base failed' }

& python (Join-Path $here 'make_fixtures.py')
if ($LASTEXITCODE -ne 0) { throw 'make_fixtures failed' }

& (Join-Path $build 'exif_oracle.exe') $fix (Join-Path $here 'exif_dump.txt')
if ($LASTEXITCODE -ne 0) { throw ("oracle crashed with exit code {0}" -f $LASTEXITCODE) }

& python (Join-Path $here 'verify.py')
if ($LASTEXITCODE -ne 0) { throw 'verification failed' }

# Marca de midia efemera: relatorio -> contagem -> pixels do selo.
& (Join-Path $build 'view_once_oracle.exe')
if ($LASTEXITCODE -ne 0) { throw ("ver-uma-vez: oracle falhou com exit code {0}" -f $LASTEXITCODE) }
