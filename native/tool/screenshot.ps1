# 给安装器窗口截图 / 模拟点击的调试脚本（开发期用，不随包分发）。
#
# 三个实测结论，别绕回去：
#  1. **不用 `PrintWindow`**：WebView2 的内容是 DirectComposition 合成的，`PrintWindow`
#     抓回来常常是一片空白 —— 截出来"看着像界面没渲染"，其实是抓法不对。
#     这里改成"把窗口提到最前 → 抓屏幕上那块矩形"，所见即所得。
#  2. **不用 `FindWindow(null, 标题)`**：本机实测它对本安装器窗口恒返回 0
#     （`GetWindowTextW` 读出来的标题与要找的字符串逐字符相同，仍然找不到，
#      FindWindowEx 也一样）。改用 `Process.MainWindowHandle`，稳定拿得到。
#  3. **`SetForegroundWindow` 会被系统的前台锁拦掉**：只调它的话截出来的是
#     别的窗口盖在安装器上面的画面（实测）。可靠做法是**临时置顶**
#     （`SetWindowPos(HWND_TOPMOST)`），截完再撤掉。
#
# 用法：
#   powershell -File screenshot.ps1 -Out shot.png
#   powershell -File screenshot.ps1 -Out shot.png -ClickX 739 -ClickY 580
param(
  [string]$Out = "shot.png",
  [int]$ClickX = -1,
  [int]$ClickY = -1,
  [string]$ProcessName = "LxAI-Setup",
  [int]$WaitMs = 1200,
  # 点完到截图之间等多久。安装 56 MB 只要几百毫秒，想抓"正在安装"那一页
  # 就得把这个值调小（默认 900 抓到的多半已经是完成页）。
  [int]$PostClickMs = 900
)

Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class LxWnd {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  public const uint LEFTDOWN = 0x0002, LEFTUP = 0x0004;
  public const uint SWP_NOSIZE = 0x0001, SWP_NOMOVE = 0x0002, SWP_NOACTIVATE = 0x0010;
  public static readonly IntPtr TOPMOST = new IntPtr(-1), NOTOPMOST = new IntPtr(-2);

  public static void Raise(IntPtr h) {
    ShowWindow(h, 9);            // SW_RESTORE
    SetWindowPos(h, TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
    BringWindowToTop(h);
    SetForegroundWindow(h);
  }
  public static void Unraise(IntPtr h) {
    SetWindowPos(h, NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
  }
  public static void Click(int x, int y) {
    SetCursorPos(x, y);
    System.Threading.Thread.Sleep(120);
    mouse_event(LEFTDOWN, 0, 0, 0, IntPtr.Zero);
    System.Threading.Thread.Sleep(60);
    mouse_event(LEFTUP, 0, 0, 0, IntPtr.Zero);
  }
}
"@

# 主窗口句柄要等窗口建出来才非零，轮询几次别急着失败
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 20; $i++) {
  $proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
          Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if ($proc) { $h = $proc.MainWindowHandle; break }
  Start-Sleep -Milliseconds 300
}
if ($h -eq [IntPtr]::Zero) { Write-Error "没找到 $ProcessName 的主窗口"; exit 1 }
Write-Host "窗口句柄：$h"

[LxWnd]::Raise($h)
Start-Sleep -Milliseconds $WaitMs

$r = New-Object LxWnd+RECT
[void][LxWnd]::GetWindowRect($h, [ref]$r)

if ($ClickX -ge 0 -and $ClickY -ge 0) {
  $sx = $r.Left + $ClickX
  $sy = $r.Top + $ClickY
  Write-Host "点击窗口内坐标 ($ClickX,$ClickY) → 屏幕 ($sx,$sy)"
  [LxWnd]::Click($sx, $sy)
  Start-Sleep -Milliseconds $PostClickMs
  [void][LxWnd]::GetWindowRect($h, [ref]$r)
}

$w = $r.Right - $r.Left
$hh = $r.Bottom - $r.Top
Write-Host "窗口矩形：$($r.Left),$($r.Top) ${w}x${hh}"

$bmp = New-Object System.Drawing.Bitmap($w, $hh)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size($w, $hh)))
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

[LxWnd]::Unraise($h)
Write-Host "已保存：$Out"
