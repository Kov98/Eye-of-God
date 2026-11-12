# language: PowerShell, file: aes_helper.ps1
# Independent AES-256-CBC/PKCS7 encryptor for the .ab oracle.
# .NET's System.Security.Cryptography is a different code path from the C++ under
# test (which drives bcrypt directly), so a mistake in the reader's CBC plumbing
# cannot be mirrored here.
# Parameter names avoid the common-parameter aliases PowerShell resolves first
# ('-Iv' collides with -InformationVariable, for instance).
param(
    [Parameter(Mandatory = $true)][string]$KeyHex,
    [Parameter(Mandatory = $true)][string]$IvHex,
    [Parameter(Mandatory = $true)][string]$InFile,
    [Parameter(Mandatory = $true)][string]$OutFile
)
$ErrorActionPreference = 'Stop'

function ConvertFrom-Hex([string]$hex) {
    if ($hex.Length % 2 -ne 0) { throw "hex com tamanho impar" }
    $bytes = New-Object byte[] ($hex.Length / 2)
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        $bytes[$i] = [Convert]::ToByte($hex.Substring($i * 2, 2), 16)
    }
    return $bytes
}

$aes = [System.Security.Cryptography.Aes]::Create()
try {
    $aes.KeySize = 256
    $aes.Mode = [System.Security.Cryptography.CipherMode]::CBC
    $aes.Padding = [System.Security.Cryptography.PaddingMode]::PKCS7
    $aes.Key = ConvertFrom-Hex $KeyHex
    $aes.IV = ConvertFrom-Hex $IvHex
    $encryptor = $aes.CreateEncryptor()
    $plain = [System.IO.File]::ReadAllBytes($InFile)
    $cipher = $encryptor.TransformFinalBlock($plain, 0, $plain.Length)
    [System.IO.File]::WriteAllBytes($OutFile, $cipher)
    $encryptor.Dispose()
} finally {
    $aes.Dispose()
}
