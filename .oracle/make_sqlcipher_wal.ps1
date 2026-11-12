# Gera um fixture SQLCipher (db + wal) a partir do banco decifrado real.
# Implementacao independente (BCL .NET): AES-CBC, HMAC-SHA1, PBKDF2 e o
# checksum do WAL, todos escritos aqui, nenhum deles vindo do codigo do produto.
Add-Type -AssemblyName System.Security
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $root
$plain = [System.IO.File]::ReadAllBytes((Join-Path $repo '.oracle\recovered\msgstore.plain.db'))
$real = [System.IO.File]::ReadAllBytes((Join-Path $repo '.oracle\msgstore2.db'))
$hex = 'AA6323D8738EEDF583777736D53F842A9D4409E3BF39A3C073EBDB72AEDBFB40'
$rawKey = New-Object byte[] 32
for ($i = 0; $i -lt 32; $i++) { $rawKey[$i] = [Convert]::ToByte($hex.Substring($i*2, 2), 16) }
$pageSize = 4096; $reserve = 48; $body = $pageSize - $reserve
$salt = New-Object byte[] 16
[Array]::Copy($real, 0, $salt, 0, 16)
$macSalt = New-Object byte[] 16
for ($i = 0; $i -lt 16; $i++) { $macSalt[$i] = $salt[$i] -bxor 0x3a }
$kdfMac = New-Object System.Security.Cryptography.Rfc2898DeriveBytes($rawKey, $macSalt, 2, [System.Security.Cryptography.HashAlgorithmName]::SHA1)
$macKey = $kdfMac.GetBytes(20)

# WAL e big-endian em todos os inteiros.
function Put-BE32([byte[]]$target, [int]$offset, [uint32]$value) {
    $bytes = [BitConverter]::GetBytes($value)
    [Array]::Reverse($bytes)
    [Array]::Copy($bytes, 0, $target, $offset, 4)
}

function Encrypt-Page([byte[]]$page, [int]$pgno) {
    $dataOff = 0
    if ($pgno -eq 1) { $dataOff = 16 }
    $pt = New-Object byte[] $body
    [Array]::Copy($page, 0, $pt, 0, $body)
    if ($pgno -eq 1) { $pt[20] = 0 }
    $iv = New-Object byte[] 16
    ([System.Security.Cryptography.RandomNumberGenerator]::Create()).GetBytes($iv)
    $aes = [System.Security.Cryptography.Aes]::Create()
    $aes.Mode = 'CBC'; $aes.Padding = 'None'; $aes.Key = $rawKey; $aes.IV = $iv
    $enc = $aes.CreateEncryptor()
    $ct = $enc.TransformFinalBlock($pt, $dataOff, $body - $dataOff)
    $aes.Dispose()
    $hmac = New-Object System.Security.Cryptography.HMACSHA1(, $macKey)
    $buf = New-Object byte[] ($ct.Length + 16 + 4)
    [Array]::Copy($ct, 0, $buf, 0, $ct.Length)
    [Array]::Copy($iv, 0, $buf, $ct.Length, 16)
    $pgnoLe = [BitConverter]::GetBytes([uint32]$pgno)    # o MAC usa o numero little-endian
    [Array]::Copy($pgnoLe, 0, $buf, $ct.Length + 16, 4)
    $mac = $hmac.ComputeHash($buf)
    $hmac.Dispose()
    $out = New-Object byte[] $pageSize
    if ($pgno -eq 1) { [Array]::Copy($salt, 0, $out, 0, 16) }
    [Array]::Copy($ct, 0, $out, $dataOff, $ct.Length)
    [Array]::Copy($iv, 0, $out, $body, 16)
    [Array]::Copy($mac, 0, $out, $body + 16, 20)
    return ,$out
}

function Get-WalChecksum([byte[]]$data, [int]$length, [uint32]$s1, [uint32]$s2) {
    [uint64]$a = $s1
    [uint64]$b = $s2
    $mask = [uint64]4294967295
    for ($i = 0; $i -lt [int]($length / 8); $i++) {
        $off = $i * 8
        # words little-endian, como o SQLite calcula em maquina x86
        [uint64]$w1 = [uint64]$data[$off] -bor ([uint64]$data[$off+1] -shl 8) -bor ([uint64]$data[$off+2] -shl 16) -bor ([uint64]$data[$off+3] -shl 24)
        [uint64]$w2 = [uint64]$data[$off+4] -bor ([uint64]$data[$off+5] -shl 8) -bor ([uint64]$data[$off+6] -shl 16) -bor ([uint64]$data[$off+7] -shl 24)
        $a = ($a + $w1 + $b) -band $mask
        $b = ($b + $w2 + $a) -band $mask
    }
    return ,@([uint32]$a, [uint32]$b)
}

$pages = [int]($plain.Length / $pageSize)
$dbOut = New-Object System.Collections.Generic.List[byte]
for ($pgno = 1; $pgno -le $pages; $pgno++) {
    $page = New-Object byte[] $pageSize
    [Array]::Copy($plain, ($pgno - 1) * $pageSize, $page, 0, $pageSize)
    [byte[]]$encPage = Encrypt-Page $page $pgno
    $dbOut.AddRange($encPage)
}
$dbPath = Join-Path $repo '.oracle\sc_wal.db'
[System.IO.File]::WriteAllBytes($dbPath, $dbOut.ToArray())

$walSalt1 = [uint32]0x11223344
$walSalt2 = [uint32]0x55667788
$header = New-Object byte[] 32
Put-BE32 $header 0 ([uint32]0x377f0682)
Put-BE32 $header 4 ([uint32]3007000)
Put-BE32 $header 8 ([uint32]$pageSize)
Put-BE32 $header 12 ([uint32]0)
Put-BE32 $header 16 $walSalt1
Put-BE32 $header 20 $walSalt2
$sums = Get-WalChecksum $header 24 ([uint32]0) ([uint32]0)
Put-BE32 $header 24 ([uint32]$sums[0])
Put-BE32 $header 28 ([uint32]$sums[1])

$walOut = New-Object System.Collections.Generic.List[byte]
$walOut.AddRange($header)
[uint32]$s1 = $sums[0]; [uint32]$s2 = $sums[1]
$markerOffset = 100
for ($k = 1; $k -le 2; $k++) {
    $page = New-Object byte[] $pageSize
    [Array]::Copy($plain, 1 * $pageSize, $page, 0, $pageSize)
    if ($k -eq 2) { $page[$markerOffset] = 0x5A }
    [byte[]]$encPage = Encrypt-Page $page 2
    $fh = New-Object byte[] 24
    Put-BE32 $fh 0 ([uint32]2)
    $dbSize = 0
    if ($k -eq 2) { $dbSize = $pages }
    Put-BE32 $fh 4 ([uint32]$dbSize)
    Put-BE32 $fh 8 $walSalt1
    Put-BE32 $fh 12 $walSalt2
    $h = Get-WalChecksum $fh 8 $s1 $s2
    $full = Get-WalChecksum $encPage $pageSize ([uint32]$h[0]) ([uint32]$h[1])
    Put-BE32 $fh 16 ([uint32]$full[0])
    Put-BE32 $fh 20 ([uint32]$full[1])
    $s1 = [uint32]$full[0]; $s2 = [uint32]$full[1]
    $walOut.AddRange($fh)
    $walOut.AddRange($encPage)
}
$walPath = $dbPath + '-wal'
[System.IO.File]::WriteAllBytes($walPath, $walOut.ToArray())
"db={0} ({1} paginas) wal={2} bytes 2 frames marcador=0x5A em {3}" -f $dbPath, $pages, $walOut.Count, $markerOffset