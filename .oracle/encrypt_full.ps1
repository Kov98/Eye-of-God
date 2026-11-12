$ErrorActionPreference = 'Stop'
$dir = "C:\Users\pedri\Downloads\Eye-of-God-main\.oracle"
$plain = [System.IO.File]::ReadAllBytes("$dir\full.db")
$psz = 4096; $reserve = 48
$pages = $plain.Length / $psz
$rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
$salt = New-Object byte[] 16; $rng.GetBytes($salt)
$rawKey = New-Object byte[] 32; $rng.GetBytes($rawKey)
$macSalt = New-Object byte[] 16
for ($i = 0; $i -lt 16; $i++) { $macSalt[$i] = $salt[$i] -bxor 0x3a }
$kdf = New-Object System.Security.Cryptography.Rfc2898DeriveBytes($rawKey, $macSalt, 2, [System.Security.Cryptography.HashAlgorithmName]::SHA1)
$macKey = $kdf.GetBytes(20)
$aes = [System.Security.Cryptography.Aes]::Create()
$aes.Mode = 'CBC'; $aes.Padding = 'None'; $aes.KeySize = 256
$hmacAlg = [System.Security.Cryptography.HMACSHA1]::new($macKey)
$out = New-Object byte[] ($pages * $psz)
for ($p = 1; $p -le $pages; $p++) {
    $srcOff = ($p - 1) * $psz
    $bodyEnd = $psz - $reserve
    $iv = New-Object byte[] 16; $rng.GetBytes($iv)
    $dataOff = if ($p -eq 1) { 16 } else { 0 }
    $len = $bodyEnd - $dataOff
    $chunk = New-Object byte[] $len
    [Array]::Copy($plain, $srcOff + $dataOff, $chunk, 0, $len)
    $aes.Key = $rawKey; $aes.IV = $iv
    $enc = $aes.CreateEncryptor().TransformFinalBlock($chunk, 0, $len)
    if ($p -eq 1) { [Array]::Copy($salt, 0, $out, 0, 16) }
    [Array]::Copy($enc, 0, $out, $srcOff + $dataOff, $enc.Length)
    $macBuf = New-Object byte[] ($enc.Length + 20)
    [Array]::Copy($enc, 0, $macBuf, 0, $enc.Length)
    [Array]::Copy($iv, 0, $macBuf, $enc.Length, 16)
    [BitConverter]::GetBytes([uint32]$p).CopyTo($macBuf, $enc.Length + 16)
    $tag = $hmacAlg.ComputeHash($macBuf)
    [Array]::Copy($iv, 0, $out, $srcOff + $bodyEnd, 16)
    [Array]::Copy($tag, 0, $out, $srcOff + $bodyEnd + 16, 20)
}
[System.IO.File]::WriteAllBytes("$dir\full.db.enc", $out)
Set-Content "$dir\full_key.txt" -Value ([BitConverter]::ToString($rawKey) -replace '-', '') -Encoding ascii -NoNewline
Write-Host ("cifrado: {0} bytes, paginas={1}" -f $out.Length, $pages)
