Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices;
public class RD {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
'@
[void][RD]::SetProcessDPIAware()
$script:p2=(Get-Process RadukCapture | Select-Object -First 1).Id
$script:w=@{}
$cb=[RD+EnumProc]{ param($x,$l)
  $p=0; [void][RD]::GetWindowThreadProcessId($x,[ref]$p)
  if ($p -eq $script:p2) { $sb=New-Object System.Text.StringBuilder 128; [void][RD]::GetClassName($x,$sb,128)
    $n=$sb.ToString()
    if ($n.StartsWith("Raduk") -and [RD]::IsWindowVisible($x) -and -not $script:w.ContainsKey($n)) { $script:w[$n]=$x } }
  return $true }
[void][RD]::EnumWindows($cb,[IntPtr]::Zero)
"window                   non-red %   (lower is better with night vision on)"
foreach ($k in ($script:w.Keys | Sort-Object)) {
  $h=[IntPtr]$script:w[$k]
  $r=New-Object RD+RECT; [void][RD]::GetWindowRect($h,[ref]$r)
  $wd=$r.R-$r.L; $ht=$r.B-$r.T
  if ($wd -le 20 -or $ht -le 20) { continue }
  if ($wd -gt 1400) { $wd = 1400 }
  if ($ht -gt 1000) { $ht = 1000 }
  $bmp=New-Object System.Drawing.Bitmap $wd,$ht
  $g=[System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size)
  $nonred=0; $n=0
  for($y=0;$y -lt $ht;$y+=5){for($x=0;$x -lt $wd;$x+=5){
    $c=$bmp.GetPixel($x,$y); $n++
    # "red" under night vision means G and B are well below R.
    if (-not ($c.R -ge $c.G + 12 -and $c.R -ge $c.B + 12)) {
      # near-black counts as fine either way
      if ($c.R -gt 24 -or $c.G -gt 24 -or $c.B -gt 24) { $nonred++ }
    }
  }}
  $g.Dispose();$bmp.Dispose()
  "{0,-24} {1,6:N1}%" -f $k, (100.0*$nonred/$n)
}
