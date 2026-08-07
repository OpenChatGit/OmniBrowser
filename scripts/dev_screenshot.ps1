param(
  [string]$OutFile = "shot.png",
  [int]$ClickOffsetRight = 0,
  [int]$ClickOffsetTop = 0,
  [switch]$Click
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, UIntPtr dwExtraInfo);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$proc = Get-Process OmniBrowser -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { Write-Error "no window"; exit 1 }
[Win32]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 400

$rect = New-Object Win32+RECT
[Win32]::GetWindowRect($proc.MainWindowHandle, [ref]$rect) | Out-Null

if ($Click) {
  $x = $rect.Right - $ClickOffsetRight
  $y = $rect.Top + $ClickOffsetTop
  [Win32]::SetCursorPos($x, $y) | Out-Null
  Start-Sleep -Milliseconds 150
  [Win32]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)  # left down
  [Win32]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)  # left up
  Start-Sleep -Milliseconds 700
}

$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
$bmp.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$gfx.Dispose(); $bmp.Dispose()
Write-Output "saved $OutFile rect=$($rect.Left),$($rect.Top),$($rect.Right),$($rect.Bottom)"
