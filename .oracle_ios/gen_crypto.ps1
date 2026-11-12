# Oráculo criptografico independente do codigo C++ que esta sendo testado.
# Tudo aqui usa exclusivamente .NET (System.Security.Cryptography); o C++ usa CNG.
# RFC 3394 e implementado a mao porque o .NET nao expoe AES Key Wrap.
# Modo de uso:
#   powershell -File gen_crypto.ps1 -Job job.json -Out result.json
#   powershell -File gen_crypto.ps1 -SelfTest -Out selftest.json

param(
    [string]$Job,
    [string]$Out,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

function NewBytes([int]$Count) { return New-Object 'byte[]' $Count }

function ToB64([byte[]]$Data) {
    if ($null -eq $Data) { return '' }
    return [Convert]::ToBase64String($Data)
}

function FromB64([string]$Text) {
    if ([string]::IsNullOrEmpty($Text)) { return NewBytes 0 }
    return [Convert]::FromBase64String($Text)
}

function FromHex([string]$Text) {
    if ([string]::IsNullOrEmpty($Text)) { return NewBytes 0 }
    $out = NewBytes ($Text.Length / 2)
    for ($i = 0; $i -lt $out.Length; $i++) {
        $out[$i] = [Convert]::ToByte($Text.Substring($i * 2, 2), 16)
    }
    return ,$out
}

function ToHex([byte[]]$Data) { return ([BitConverter]::ToString($Data) -replace '-', '').ToLowerInvariant() }

function AesEcbBlock {
    param([byte[]]$Key, [byte[]]$Block, [bool]$Encrypt)
    $aes = [System.Security.Cryptography.Aes]::Create()
    try {
        $aes.Mode = [System.Security.Cryptography.CipherMode]::ECB
        $aes.Padding = [System.Security.Cryptography.PaddingMode]::None
        $aes.Key = $Key
        if ($Encrypt) { $t = $aes.CreateEncryptor() } else { $t = $aes.CreateDecryptor() }
        try { return ,$t.TransformFinalBlock($Block, 0, $Block.Length) } finally { $t.Dispose() }
    } finally { $aes.Dispose() }
}

function AesKeyWrap {
    param([byte[]]$Kek, [byte[]]$Plain)
    if ($Plain.Length -lt 16 -or ($Plain.Length % 8) -ne 0) { throw 'AES-KW: entrada invalida' }
    $n = [int]($Plain.Length / 8)
    [byte[]]$a = @(0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6)
    [byte[]]$r = NewBytes $Plain.Length
    [Array]::Copy($Plain, $r, $Plain.Length)
    $blk = NewBytes 16
    for ($j = 0; $j -lt 6; $j++) {
        for ($i = 1; $i -le $n; $i++) {
            [Array]::Copy($a, 0, $blk, 0, 8)
            [Array]::Copy($r, ($i - 1) * 8, $blk, 8, 8)
            $b = AesEcbBlock -Key $Kek -Block $blk -Encrypt $true
            [Array]::Copy($b, 0, $a, 0, 8)
            [UInt64]$t = [UInt64]$n * [UInt64]$j + [UInt64]$i
            for ($k = 0; $k -lt 8; $k++) {
                $a[7 - $k] = [byte]($a[7 - $k] -bxor [byte](($t -shr (8 * $k)) -band 0xFF))
            }
            [Array]::Copy($b, 8, $r, ($i - 1) * 8, 8)
        }
    }
    $out = NewBytes (8 + $r.Length)
    [Array]::Copy($a, 0, $out, 0, 8)
    [Array]::Copy($r, 0, $out, 8, $r.Length)
    return ,$out
}

function AesKeyUnwrap {
    param([byte[]]$Kek, [byte[]]$Wrapped)
    if ($Wrapped.Length -lt 24 -or ($Wrapped.Length % 8) -ne 0) { throw 'AES-KW: dado embrulhado invalido' }
    $n = [int]($Wrapped.Length / 8 - 1)
    [byte[]]$a = NewBytes 8
    [Array]::Copy($Wrapped, 0, $a, 0, 8)
    [byte[]]$r = NewBytes ($n * 8)
    [Array]::Copy($Wrapped, 8, $r, 0, $n * 8)
    $blk = NewBytes 16
    for ($j = 5; $j -ge 0; $j--) {
        for ($i = $n; $i -ge 1; $i--) {
            [UInt64]$t = [UInt64]$n * [UInt64]$j + [UInt64]$i
            for ($k = 0; $k -lt 8; $k++) {
                $blk[7 - $k] = [byte]($a[7 - $k] -bxor [byte](($t -shr (8 * $k)) -band 0xFF))
            }
            [Array]::Copy($r, ($i - 1) * 8, $blk, 8, 8)
            $b = AesEcbBlock -Key $Kek -Block $blk -Encrypt $false
            [Array]::Copy($b, 0, $a, 0, 8)
            [Array]::Copy($b, 8, $r, ($i - 1) * 8, 8)
        }
    }
    [byte[]]$iv = @(0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6)
    for ($k = 0; $k -lt 8; $k++) {
        if ($a[$k] -ne $iv[$k]) { throw 'AES-KW: integrity check falhou' }
    }
    return ,$r
}

# PBKDF2 escrito a mao sobre HMACSHA256: prova que o C++ implementa o RFC 2898,
# nao apenas que concorda com uma chamada de biblioteca.
function Pbkdf2Sha256Manual {
    param([byte[]]$Password, [byte[]]$Salt, [int]$Iterations, [int]$Length)
    $hmac = [System.Security.Cryptography.HMACSHA256]::new($Password)
    try {
        $blocks = [int][Math]::Ceiling($Length / 32.0)
        $out = New-Object 'System.Collections.Generic.List[byte]'
        for ($i = 1; $i -le $blocks; $i++) {
            $input = NewBytes ($Salt.Length + 4)
            [Array]::Copy($Salt, 0, $input, 0, $Salt.Length)
            $input[$Salt.Length + 0] = [byte](($i -shr 24) -band 0xFF)
            $input[$Salt.Length + 1] = [byte](($i -shr 16) -band 0xFF)
            $input[$Salt.Length + 2] = [byte](($i -shr 8) -band 0xFF)
            $input[$Salt.Length + 3] = [byte]($i -band 0xFF)
            [byte[]]$u = $hmac.ComputeHash($input)
            [byte[]]$acc = NewBytes 32
            [Array]::Copy($u, $acc, 32)
            for ($c = 1; $c -lt $Iterations; $c++) {
                $u = $hmac.ComputeHash($u)
                for ($k = 0; $k -lt 32; $k++) { $acc[$k] = $acc[$k] -bxor $u[$k] }
            }
            $out.AddRange($acc)
        }
        $result = $out.ToArray()
        if ($result.Length -gt $Length) {
            $trimmed = NewBytes $Length
            [Array]::Copy($result, $trimmed, $Length)
            $result = $trimmed
        }
        return ,$result
    } finally { $hmac.Dispose() }
}

function Pbkdf2Sha256Library {
    param([byte[]]$Password, [byte[]]$Salt, [int]$Iterations, [int]$Length)
    $kdf = New-Object System.Security.Cryptography.Rfc2898DeriveBytes($Password, $Salt, $Iterations, [System.Security.Cryptography.HashAlgorithmName]::SHA256)
    try { return ,$kdf.GetBytes($Length) } finally { $kdf.Dispose() }
}

function AesCbcEncryptPkcs7 {
    param([byte[]]$Key, [byte[]]$Iv, [byte[]]$Data)
    $aes = [System.Security.Cryptography.Aes]::Create()
    try {
        $aes.Mode = [System.Security.Cryptography.CipherMode]::CBC
        $aes.Padding = [System.Security.Cryptography.PaddingMode]::PKCS7
        $aes.Key = $Key
        $aes.IV = $Iv
        $enc = $aes.CreateEncryptor()
        try { return ,$enc.TransformFinalBlock($Data, 0, $Data.Length) } finally { $enc.Dispose() }
    } finally { $aes.Dispose() }
}

function AesCbcDecryptPkcs7 {
    param([byte[]]$Key, [byte[]]$Iv, [byte[]]$Data)
    $aes = [System.Security.Cryptography.Aes]::Create()
    try {
        $aes.Mode = [System.Security.Cryptography.CipherMode]::CBC
        $aes.Padding = [System.Security.Cryptography.PaddingMode]::PKCS7
        $aes.Key = $Key
        $aes.IV = $Iv
        $dec = $aes.CreateDecryptor()
        try { return ,$dec.TransformFinalBlock($Data, 0, $Data.Length) } finally { $dec.Dispose() }
    } finally { $aes.Dispose() }
}

function RunSelfTest {
    $checks = New-Object 'System.Collections.Generic.List[object]'
    function AddCheck([string]$Name, [bool]$Ok, [string]$Got, [string]$Want) {
        $checks.Add([pscustomobject]@{ name = $Name; ok = $Ok; got = $Got; want = $Want })
    }

    # RFC 3394 secao 4.1 - KEK 128 bits, chave 128 bits
    $kek = FromHex '000102030405060708090A0B0C0D0E0F'
    $plain = FromHex '00112233445566778899AABBCCDDEEFF'
    $want = '1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5'
    $got = ToHex (AesKeyWrap -Kek $kek -Plain $plain)
    AddCheck 'rfc3394_4.1_wrap' ($got -eq $want) $got $want
    $back = ToHex (AesKeyUnwrap -Kek $kek -Wrapped (FromHex $got))
    AddCheck 'rfc3394_4.1_unwrap' ($back -eq '00112233445566778899aabbccddeeff') $back '00112233445566778899aabbccddeeff'

    # RFC 3394 secao 4.6 - KEK 256 bits, chave 256 bits
    $kek = FromHex '000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F'
    $plain = FromHex '00112233445566778899AABBCCDDEEFF000102030405060708090A0B0C0D0E0F'
    $want = '28c9f404c4b810f4cbccb35cfb87f8263f5786e2d80ed326cbc7f0e71a99f43bfb988b9b7a02dd21'
    $got = ToHex (AesKeyWrap -Kek $kek -Plain $plain)
    AddCheck 'rfc3394_4.6_wrap' ($got -eq $want) $got $want

    # NIST SP 800-38A F.2.5 - AES-256-CBC encrypt
    $key = FromHex '603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4'
    $iv = FromHex '000102030405060708090A0B0C0D0E0F'
    $plain = FromHex '6bc1bee22e409f96e93d7e117393172a'
    $want = 'f58c4c04d6e5f1ba779eabfb5f7bfbd6'
    $aes = [System.Security.Cryptography.Aes]::Create()
    $aes.Mode = [System.Security.Cryptography.CipherMode]::CBC
    $aes.Padding = [System.Security.Cryptography.PaddingMode]::None
    $aes.Key = $key
    $aes.IV = $iv
    $enc = $aes.CreateEncryptor()
    $got = ToHex ($enc.TransformFinalBlock($plain, 0, 16))
    $enc.Dispose()
    $aes.Dispose()
    AddCheck 'nist_sp800_38a_aes256cbc' ($got -eq $want) $got $want

    # PBKDF2-HMAC-SHA256 vetores publicados (password/salt, iteracoes)
    $vectors = @(
        @{ password = 'password'; salt = 'salt'; iterations = 1;
           want = '120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b' },
        @{ password = 'password'; salt = 'salt'; iterations = 2;
           want = 'ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43' },
        @{ password = 'password'; salt = 'salt'; iterations = 4096;
           want = 'c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a' }
    )
    foreach ($vector in $vectors) {
        $password = [System.Text.Encoding]::UTF8.GetBytes($vector.password)
        $salt = [System.Text.Encoding]::UTF8.GetBytes($vector.salt)
        $got = ToHex (Pbkdf2Sha256Manual -Password $password -Salt $salt -Iterations $vector.iterations -Length 32)
        AddCheck ("pbkdf2_sha256_i" + $vector.iterations) ($got -eq $vector.want) $got $vector.want
    }

    # A implementacao manual e a da biblioteca precisam concordar.
    $password = [System.Text.Encoding]::UTF8.GetBytes('senha-com-acentuacao-çãé')
    $salt = NewBytes 20
    for ($i = 0; $i -lt 20; $i++) { $salt[$i] = [byte](($i * 37 + 11) -band 0xFF) }
    $manual = ToHex (Pbkdf2Sha256Manual -Password $password -Salt $salt -Iterations 137 -Length 32)
    $library = ToHex (Pbkdf2Sha256Library -Password $password -Salt $salt -Iterations 137 -Length 32)
    AddCheck 'pbkdf2_manual_vs_library' ($manual -eq $library) $manual $library

    # Ida e volta do key wrap em tamanhos variados.
    $roundTripOk = $true
    $detail = @()
    foreach ($size in 16, 24, 32, 40, 64) {
        $kek = NewBytes 32
        for ($i = 0; $i -lt 32; $i++) { $kek[$i] = [byte](($i * 7 + 3) -band 0xFF) }
        $data = NewBytes $size
        for ($i = 0; $i -lt $size; $i++) { $data[$i] = [byte](($i * 13 + 5) -band 0xFF) }
        $wrapped = AesKeyWrap -Kek $kek -Plain $data
        $back = AesKeyUnwrap -Kek $kek -Wrapped $wrapped
        if ((ToHex $back) -ne (ToHex $data)) { $roundTripOk = $false }
        $detail += ("{0}->{1}" -f $size, $wrapped.Length)
    }
    AddCheck 'aes_kw_roundtrip' $roundTripOk ($detail -join ',') 'identidade'

    return $checks
}

if ($SelfTest) {
    $checks = RunSelfTest
    $passed = ($checks | Where-Object { $_.ok }).Count
    $result = [pscustomobject]@{
        total = $checks.Count
        passed = $passed
        checks = $checks
    }
    $json = $result | ConvertTo-Json -Depth 6
    if ($Out) { Set-Content -Path $Out -Value $json -Encoding UTF8 } else { Write-Output $json }
    exit 0
}

if (-not $Job) { Write-Error 'informe -Job ou -SelfTest'; exit 2 }
$spec = Get-Content -Raw -Path $Job | ConvertFrom-Json
$results = @()
foreach ($op in $spec.ops) {
    $entry = [ordered]@{ id = $op.id; op = $op.op; ok = $true }
    try {
        switch ($op.op) {
            'pbkdf2' {
                $entry.hex = ToHex (Pbkdf2Sha256Library -Password (FromB64 $op.password_b64) -Salt (FromB64 $op.salt_b64) -Iterations ([int]$op.iterations) -Length ([int]$op.length))
            }
            'kw_wrap' {
                $entry.b64 = ToB64 (AesKeyWrap -Kek (FromB64 $op.kek_b64) -Plain (FromB64 $op.plain_b64))
            }
            'kw_unwrap' {
                $entry.b64 = ToB64 (AesKeyUnwrap -Kek (FromB64 $op.kek_b64) -Wrapped (FromB64 $op.wrapped_b64))
            }
            'aes_cbc_encrypt' {
                $entry.b64 = ToB64 (AesCbcEncryptPkcs7 -Key (FromB64 $op.key_b64) -Iv (FromB64 $op.iv_b64) -Data (FromB64 $op.data_b64))
            }
            'aes_cbc_decrypt' {
                $entry.b64 = ToB64 (AesCbcDecryptPkcs7 -Key (FromB64 $op.key_b64) -Iv (FromB64 $op.iv_b64) -Data (FromB64 $op.data_b64))
            }
            default { throw "op desconhecida: $($op.op)" }
        }
    } catch {
        $entry.ok = $false
        $entry.error = $_.Exception.Message
    }
    $results += [pscustomobject]$entry
}
$payload = [pscustomobject]@{ results = $results }
$json = $payload | ConvertTo-Json -Depth 6 -Compress
Set-Content -Path $Out -Value $json -Encoding UTF8
exit 0
