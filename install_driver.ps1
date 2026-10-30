# Requires an elevated PowerShell and test signing. Loads the self-signed
# OlhoDeDeusKernel driver for local testing only.
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated PowerShell.'
}
$root = $PSScriptRoot
$driver = Join-Path $root 'OlhoDeDeus.sys'
$certificate = Join-Path $root 'OlhoDeDeus.cer'
if (-not (Test-Path $driver)) { throw "Driver not found: $driver" }
if (-not (Test-Path $certificate)) { throw "Certificate not found: $certificate" }

Import-Certificate -FilePath $certificate -CertStoreLocation 'Cert:\LocalMachine\Root' | Out-Null
Import-Certificate -FilePath $certificate -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher' | Out-Null
bcdedit.exe /set testsigning on
sc.exe create OlhoDeDeus type= kernel start= demand binPath= "`"$driver`""
sc.exe start OlhoDeDeus
Write-Host 'Driver loaded. Reboot is required if testsigning was just enabled.'
Write-Host 'Remove with: sc.exe stop OlhoDeDeus; sc.exe delete OlhoDeDeus; bcdedit /set testsigning off'
