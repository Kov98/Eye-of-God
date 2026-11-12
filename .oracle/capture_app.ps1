param([string]$ProcName = "OlhoDeDeus_gui")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Cap2 {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT2 r);
  public struct RECT2 { public int Left, Top, Right, Bottom; }
}
"@
$p = Get-Process $ProcName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { "processo ausente: $ProcName"; exit 1 }
$h = $p.MainWindowHandle
if ($h -eq 0) { "sem hwnd"; exit 1 }
$r = New-Object Cap2+RECT2
[void][Cap2]::GetWindowRect($h, [ref]$r)
$w = $r.Right - $r.Left
$ht = $r.Bottom - $r.Top
"hwnd=$h tamanho=" + $w + "x" + $ht
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[void][Cap2]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$g.Dispose()
$out = "C:\Users\pedri\Downloads\Eye-of-God-main\.oracle\app_capture.png"
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
"salvo: $out"