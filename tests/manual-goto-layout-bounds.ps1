Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices; using System.Collections.Generic;
public class GL {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x,int y,int w,int t,bool rp);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
}
'@
[void][GL]::SetProcessDPIAware()
$script:p2=(Get-Process RadukCapture | Select-Object -First 1).Id; $script:h=[IntPtr]::Zero
$cb=[GL+EnumProc]{ param($x,$l)
  $p=0; [void][GL]::GetWindowThreadProcessId($x,[ref]$p)
  if ($p -eq $script:p2) { $sb=New-Object System.Text.StringBuilder 128; [void][GL]::GetClassName($x,$sb,128)
    if ($sb.ToString() -eq "RadukGoToPanel") { $script:h=$x } }
  return $true }
[void][GL]::EnumWindows($cb,[IntPtr]::Zero)
if ($script:h -eq [IntPtr]::Zero) { "goto panel not found"; exit 1 }
$h=$script:h; [void][GL]::ShowWindow($h,9); Start-Sleep -Milliseconds 400; [void][GL]::ShowWindow($h,5)

foreach ($sz in @(@(480,700,'gt_a.png'), @(620,900,'gt_b.png'), @(1000,1040,'gt_c.png'))) {
  [void][GL]::MoveWindow($h,60,60,$sz[0],$sz[1],$true); Start-Sleep -Milliseconds 1100
  $cr=New-Object GL+RECT; [void][GL]::GetClientRect($h,[ref]$cr)
  $script:bad = New-Object System.Collections.ArrayList
  $script:n = 0
  $ccb=[GL+EnumProc]{ param($c,$l)
    if (-not [GL]::IsWindowVisible($c)) { return $true }
    $r=New-Object GL+RECT; [void][GL]::GetWindowRect($c,[ref]$r)
    $tl=New-Object GL+POINT; $tl.X=$r.L; $tl.Y=$r.T; [void][GL]::ScreenToClient($h,[ref]$tl)
    $br=New-Object GL+POINT; $br.X=$r.R; $br.Y=$r.B; [void][GL]::ScreenToClient($h,[ref]$br)
    $script:n++
    $t=New-Object System.Text.StringBuilder 128; [void][GL]::GetWindowTextW($c,$t,128)
    if ($br.Y -gt $cr.B -or $br.X -gt $cr.R -or $tl.X -lt 0 -or $tl.Y -lt 0) {
      [void]$script:bad.Add(("{0} [{1},{2}..{3},{4}]" -f $t.ToString().Substring(0,[Math]::Min(22,$t.Length)), $tl.X,$tl.Y,$br.X,$br.Y))
    }
    return $true }
  [void][GL]::EnumChildWindows($h,$ccb,[IntPtr]::Zero)
  "client {0}x{1}: {2} visible controls, {3} outside" -f $cr.R,$cr.B,$script:n,$script:bad.Count
  foreach ($b in $script:bad) { "    OUTSIDE: $b" }
  $wr=New-Object GL+RECT; [void][GL]::GetWindowRect($h,[ref]$wr)
  $bmp=New-Object System.Drawing.Bitmap (($wr.R-$wr.L),($wr.B-$wr.T))
  $g=[System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($wr.L,$wr.T,0,0,$bmp.Size)
  $bmp.Save("C:\Users\malig\AppData\Local\Temp\claude\D--RadukCapture\813bf01e-7524-43a3-941a-a4d975abfdd7\scratchpad\$($sz[2])",[System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
}
