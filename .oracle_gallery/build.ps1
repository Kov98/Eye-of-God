#Requires -Version 5.1
# Builds the task-4 oracle binaries. The warning counter is the acceptance gate:
# galleryview.cpp has to compile under -Wall -Wextra with zero warnings.
#
# Native processes are launched through Start-Process on purpose: Windows
# PowerShell turns a compiler's stderr into a terminating error record under
# $ErrorActionPreference = 'Stop', which would abort the build on the first
# diagnostic instead of reporting it.
$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
$build = Join-Path $here 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null

$gpp = 'C:\msys64\ucrt64\bin\g++.exe'
if (-not (Test-Path $gpp)) { $gpp = (Get-Command g++ -ErrorAction Stop).Source }
$pkg = 'C:\msys64\ucrt64\bin\pkg-config.exe'
if (-not (Test-Path $pkg)) { $pkg = (Get-Command pkg-config -ErrorAction Stop).Source }

$pkgOut = Join-Path $build 'pkgconfig.txt'
$pkgProc = Start-Process -FilePath $pkg -ArgumentList @('--cflags', '--libs', 'Qt6Widgets', 'Qt6Network') -NoNewWindow -Wait -PassThru -RedirectStandardOutput $pkgOut
if ($pkgProc.ExitCode -ne 0) { throw 'pkg-config failed for Qt6Widgets Qt6Network' }
$qtFlags = ((Get-Content -Raw $pkgOut) -split '\s+') | Where-Object { $_ -ne '' }

$common = @('-std=c++17', '-O2', '-fms-extensions', '-Wall', '-Wextra', '-I', $root)

$script:warnings = 0
$script:failed = @()

function Compile([string]$label, [string[]]$arguments) {
    $stdout = Join-Path $build ($label + '.stdout.txt')
    $stderr = Join-Path $build ($label + '.stderr.txt')
    $proc = Start-Process -FilePath $gpp -ArgumentList $arguments -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $text = ''
    if (Test-Path $stdout) { $text += (Get-Content -Raw $stdout) }
    if (Test-Path $stderr) { $text += (Get-Content -Raw $stderr) }
    Set-Content -Path (Join-Path $build ($label + '.log')) -Value $text
    $count = @($text -split '\n' | Where-Object { $_ -match 'warning:' }).Count
    $script:warnings += $count
    if ($proc.ExitCode -ne 0) {
        Write-Host ("FAIL {0} exit={1}" -f $label, $proc.ExitCode)
        Write-Host $text
        $script:failed += $label
        return
    }
    Write-Host ("ok   {0,-16} warnings={1}" -f $label, $count)
    if ($count -gt 0) { Write-Host $text }
}

$gallery = Join-Path $root 'galleryview.cpp'
# A galeria usa export_publish (manifesto da grade): o modulo entra em todo alvo
# que liga galleryview.cpp, senao o link quebra com publish_export indefinido.
$exportPublish = Join-Path $root 'export_publish.cpp'
$oracle = Join-Path $here 'exif_oracle.cpp'
$harness = Join-Path $here 'harness_main.cpp'
$genbase = Join-Path $here 'gen_base.cpp'

Compile 'galleryview.o' ($common + @('-fPIC', '-c', $gallery, '-o', (Join-Path $build 'galleryview.o')) + $qtFlags)
Compile 'exif_oracle.exe' ($common + @($oracle, $gallery, $exportPublish, '-o', (Join-Path $build 'exif_oracle.exe')) + $qtFlags)
Compile 'harness.exe' ($common + @('-mwindows', $harness, $gallery, $exportPublish, '-o', (Join-Path $build 'harness.exe')) + $qtFlags)
Compile 'gen_base.exe' ($common + @($genbase, '-o', (Join-Path $build 'gen_base.exe')) + $qtFlags)
Compile 'view_once_oracle.exe' ($common + @((Join-Path $here 'view_once_oracle.cpp'), $gallery, $exportPublish, '-o', (Join-Path $build 'view_once_oracle.exe')) + $qtFlags)
Compile 'scan_bench.exe' ($common + @((Join-Path $here 'scan_bench.cpp'), $gallery, $exportPublish, '-o', (Join-Path $build 'scan_bench.exe')) + $qtFlags + @('-lpsapi'))

Write-Host ''
if ($script:failed.Count -gt 0) { throw ("build failed: " + ($script:failed -join ', ')) }
if ($script:warnings -ne 0) { throw ("warning gate failed: {0} warnings" -f $script:warnings) }
Write-Host 'all targets built, 0 warnings under -Wall -Wextra'
