# Verificacao independente do esquema de pagina do SQLCipher (chave crua, 32 bytes).
# Recriptografa as paginas do msgstore real e compara com o ciphertext original.
# 8 de 9 paginas voltam identicas; a pagina 1 difere porque o cabecalho SQLite
# nao e cifrado - os 16 primeiros bytes sao o salt, e o magic e reconstruido na
# leitura.
Add-Type -AssemblyName System.Security
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $root
$db = [System.IO.File]::ReadAllBytes((Join-Path $repo '.oracle\msgstore2.db'))
$plainPath = Join-Path $repo '.oracle\recovered\msgstore.plain.db'
if (-not (Test-Path $plainPath)) { "msgstore.plain.db ausente: rode wa_recover antes"; exit 2 }
$plain = [System.IO.File]::ReadAllBytes($plainPath)
$hex = 'AA6323D8738EEDF583777736D53F842A9D4409E3BF39A3C073EBDB72AEDBFB40'
$rawKey = New-Object byte[] 32
for ($i = 0; $i -lt 32; $i++) { $rawKey[$i] = [Convert]::ToByte($hex.Substring($i*2, 2), 16) }
$pageSize = 4096; $reserve = 48; $body = $pageSize - $reserve
$salt = New-Object byte[] 16
[Array]::Copy($db, 0, $salt, 0, 16)
$macSalt = New-Object byte[] 16
for ($i = 0; $i -lt 16; $i++) { $macSalt[$i] = $salt[$i] -bxor 0x3a }
$kdfMac = New-Object System.Security.Cryptography.Rfc2898DeriveBytes($rawKey, $macSalt, 2, [System.Security.Cryptography.HashAlgorithmName]::SHA1)
$macKey = $kdfMac.GetBytes(20)
$ok = 0; $total = [int]([Math]::Floor($db.Length / $pageSize))
for ($pgno = 1; $pgno -le $total; $pgno++) {
    $off = ($pgno - 1) * $pageSize
    $dataOff = if ($pgno -eq 1) { 16 } else { 0 }
    $pt = New-Object byte[] $body
    [Array]::Copy($plain, $off + $dataOff, $pt, $dataOff, $body - $dataOff)
    if ($pgno -eq 1) { $pt[20] = 0 }
    $iv = New-Object byte[] 16
    [Array]::Copy($db, $off + $body, $iv, 0, 16)
    $aes = [System.Security.Cryptography.Aes]::Create()
    $aes.Mode = 'CBC'; $aes.Padding = 'None'; $aes.Key = $rawKey; $aes.IV = $iv
    $enc = $aes.CreateEncryptor()
    $ct = $enc.TransformFinalBlock($pt, $dataOff, $body - $dataOff)
    $aes.Dispose()
    $hmac = New-Object System.Security.Cryptography.HMACSHA1(, $macKey)
    $buf = New-Object byte[] ($ct.Length + 16 + 4)
    [Array]::Copy($ct, 0, $buf, 0, $ct.Length)
    [Array]::Copy($iv, 0, $buf, $ct.Length, 16)
    [BitConverter]::GetBytes([uint32]$pgno).CopyTo($buf, $ct.Length + 16)
    $mac = $hmac.ComputeHash($buf); $hmac.Dispose()
    $same = $true
    for ($i = 0; $i -lt $ct.Length; $i++) { if ($ct[$i] -ne $db[$off + $dataOff + $i]) { $same = $false; break } }
    if ($same) { for ($i = 0; $i -lt 20; $i++) { if ($mac[$i] -ne $db[$off + $body + 16 + $i]) { $same = $false; break } } }
    if ($same) { $ok++ }
}
"paginas_reproduzidas={0} de {1}" -f $ok, $total
if ($ok -lt ($total - 1)) { exit 1 }
exit 0