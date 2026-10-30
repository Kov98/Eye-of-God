param(
    [string]$Root = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere not found' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found: $vcvars" }

$wdk = Join-Path $Root 'third_party\wdk\c'
$kmInclude = Join-Path $wdk 'Include\10.0.26100.0\km'
$kmCrt = Join-Path $kmInclude 'crt'
$kmLib = Join-Path $wdk 'Lib\10.0.26100.0\km\x64'
foreach ($path in @($kmInclude, $kmCrt, $kmLib)) {
    if (-not (Test-Path $path)) { throw "WDK path missing: $path" }
}
$source = Join-Path $Root 'driver\OlhoDeDeus.c'
$object = Join-Path $Root 'driver\OlhoDeDeus.obj'
$sys = Join-Path $Root 'driver\OlhoDeDeus.sys'

$command = @(
    "call `"$vcvars`" >nul",
    "cl /nologo /kernel /GS- /O2 /W4 /D_AMD64_ /DAMD64 /DWIN64 /D_WIN64 /DNT /D_KERNEL_ /I`"$kmInclude`" /I`"$kmCrt`" /c `"$source`" /Fo`"$object`"",
    "link /nologo /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry /MACHINE:X64 /OUT:`"$sys`" /LIBPATH:`"$kmLib`" `"$object`" ntoskrnl.lib hal.lib wdm.lib ntstrsafe.lib"
) -join ' && '
cmd /c $command
if ($LASTEXITCODE -ne 0) { throw "driver build failed: $LASTEXITCODE" }
$signTool = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
$makeCert = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\10.0.26100.0\x64\MakeCert.exe'
$cer = Join-Path $Root 'driver\OlhoDeDeus.cer'
if (Test-Path $signTool) {
    $cert = Get-ChildItem Cert:\CurrentUser\PrivateCertStore -ErrorAction SilentlyContinue |
        Where-Object { $_.Subject -eq 'CN=OlhoDeDeusTest' } | Select-Object -First 1
    if (-not $cert -and (Test-Path $makeCert)) {
        & $makeCert -r -pe -ss PrivateCertStore -n 'CN=OlhoDeDeusTest' $cer | Out-Null
        $cert = Get-ChildItem Cert:\CurrentUser\PrivateCertStore |
            Where-Object { $_.Subject -eq 'CN=OlhoDeDeusTest' } | Select-Object -First 1
    }
    if ($cert) {
        if (-not (Test-Path $cer)) {
            Export-Certificate -Cert $cert -FilePath $cer | Out-Null
        }
        & $signTool sign /s PrivateCertStore /sha1 $cert.Thumbprint /fd SHA256 /a $sys | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "driver signing failed: $LASTEXITCODE" }
    }
}
Get-Item $sys, $cer -ErrorAction SilentlyContinue | Select-Object FullName, Length
