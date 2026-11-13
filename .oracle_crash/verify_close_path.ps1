# Requires -Version 5.1
# Exercita o caminho que o usuario realmente faz: abre a GUI normal, espera o
# caso carregar e manda WM_CLOSE na janela. E no fechamento que o crash #9
# acontecia, entao este script cobre o caminho interativo, nao so o --screenshot.
# Uso: powershell -File .oracle_crash\verify_close_path.ps1 [-Rounds 6]
param([int]$Rounds = 6)
$ErrorActionPreference = 'Continue'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
Set-Location $root
Remove-Item (Join-Path $root 'crash-*.txt') -Force -ErrorAction SilentlyContinue

Add-Type -Namespace Win32 -Name User32 -MemberDefinition @'
[DllImport("user32.dll", SetLastError = true)]
public static extern bool PostMessageW(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
'@

$ok = 0
$failed = @()
for ($i = 1; $i -le $Rounds; ++$i) {
    $p = Start-Process -FilePath '.\OlhoDeDeus_gui.exe' -PassThru
    # espera a janela existir (ate 25 s)
    $deadline = (Get-Date).AddSeconds(25)
    while ((Get-Date) -lt $deadline -and $p.MainWindowHandle -eq 0 -and -not $p.HasExited) {
        Start-Sleep -Milliseconds 250
        $p.Refresh()
    }
    if ($p.HasExited) {
        Write-Host ('rodada ' + $i + ': saiu antes de abrir janela, codigo ' + $p.ExitCode)
        $failed += $i
        continue
    }
    Start-Sleep -Seconds 7          # deixa o caso carregar e a Timeline rodar
    $hwnd = $p.MainWindowHandle
    [void][Win32.User32]::PostMessageW($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)   # WM_CLOSE
    if (-not $p.WaitForExit(30000)) {
        Write-Host ('rodada ' + $i + ': nao fechou em 30 s (kill)')
        $p.Kill()
        $failed += $i
        continue
    }
    if ($p.ExitCode -eq 0) {
        Write-Host ('rodada ' + $i + ': fechou limpo')
        $ok++
    } else {
        Write-Host ('rodada ' + $i + ': CRASH codigo ' + $p.ExitCode)
        $failed += $i
    }
    Start-Sleep -Milliseconds 500
}

$reports = @(Get-ChildItem (Join-Path $root 'crash-*.txt') -ErrorAction SilentlyContinue)
Write-Host ''
Write-Host ('fechamentos limpos=' + $ok + ' de ' + $Rounds + '   relatorios de crash=' + $reports.Count)
if ($reports.Count -gt 0) { Get-Content $reports[0].FullName | Select-Object -First 10 | ForEach-Object { Write-Host ('  ' + $_) } }
if ($failed.Count -gt 0) { exit 1 }
exit 0
