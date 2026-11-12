# Probe: WAL cujos frames carregam o ciphertext exato da pagina 2 do proprio .db.
# Se ESTE replay nao aplicar, o problema esta na leitura do frame, nao na cifra.
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $root
$db = [System.IO.File]::ReadAllBytes((Join-Path $repo '.oracle\sc_wal.db'))
$pageSize = 4096
$page2 = New-Object byte[] $pageSize
[Array]::Copy($db, $pageSize, $page2, 0, $pageSize)   # pagina 2

function Wal-Checksum([byte[]]$data, [int]$length, [uint32]$s1, [uint32]$s2) {
    $a = $s1; $b = $s2
    $mask = [uint64]4294967295
    for ($i = 0; $i -lt [int]($length / 8); $i++) {
        $off = $i * 8
        $w1 = [uint32]$data[$off] -bor [uint32]($data[$off+1] -shl 8) -bor [uint32]($data[$off+2] -shl 16) -bor [uint32]($data[$off+3] -shl 24)
        $w2 = [uint32]$data[$off+4] -bor [uint32]($data[$off+5] -shl 8) -bor [uint32]($data[$off+6] -shl 16) -bor [uint32]($data[$off+7] -shl 24)
        $a = [uint32]((([uint64]$a + [uint64]$w1 + [uint64]$b) -band $mask))
        $b = [uint32]((([uint64]$b + [uint64]$w2 + [uint64]$a) -band $mask))
    }
    return ,@($a, $b)
}

$salt1 = [uint32]0x11223344; $salt2 = [uint32]0x55667788
$header = New-Object byte[] 32
[BitConverter]::GetBytes([uint32]0x377f0682).CopyTo($header, 0)
[BitConverter]::GetBytes([uint32]3007000).CopyTo($header, 4)
[BitConverter]::GetBytes([uint32]$pageSize).CopyTo($header, 8)
[BitConverter]::GetBytes($salt1).CopyTo($header, 16)
[BitConverter]::GetBytes($salt2).CopyTo($header, 20)
$sums = Wal-Checksum $header 24 ([uint32]0) ([uint32]0)
[BitConverter]::GetBytes([uint32]$sums[0]).CopyTo($header, 24)
[BitConverter]::GetBytes([uint32]$sums[1]).CopyTo($header, 28)

$out = New-Object System.Collections.Generic.List[byte]
$out.AddRange($header)
$s1 = [uint32]$sums[0]; $s2 = [uint32]$sums[1]
$fh = New-Object byte[] 24
[BitConverter]::GetBytes([uint32]2).CopyTo($fh, 0)
[BitConverter]::GetBytes([uint32]9).CopyTo($fh, 4)
[BitConverter]::GetBytes($salt1).CopyTo($fh, 8)
[BitConverter]::GetBytes($salt2).CopyTo($fh, 12)
$h = Wal-Checksum $fh 8 $s1 $s2
$full = Wal-Checksum $page2 $pageSize ([uint32]$h[0]) ([uint32]$h[1])
[BitConverter]::GetBytes([uint32]$full[0]).CopyTo($fh, 16)
[BitConverter]::GetBytes([uint32]$full[1]).CopyTo($fh, 20)
$out.AddRange($fh)
$out.AddRange($page2)
[System.IO.File]::WriteAllBytes((Join-Path $repo '.oracle\sc_probe.db-wal'), $out.ToArray())
Copy-Item (Join-Path $repo '.oracle\sc_wal.db') (Join-Path $repo '.oracle\sc_probe.db') -Force
"probe wal=" + $out.Count + " bytes"