Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices;
public class NV2 {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte s, uint f, IntPtr e);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
'@
[void][NV2]::SetProcessDPIAware()
$script:p2=(Get-Process RadukCapture | Select-Object -First 1).Id
$script:all = New-Object System.Collections.ArrayList
$cb=[NV2+EnumProc]{ param($x,$l)
  $p=0; [void][NV2]::GetWindowThreadProcessId($x,[ref]$p)
  if ($p -eq $script:p2) {
    $sb=New-Object System.Text.StringBuilder 128; [void][NV2]::GetClassName($x,$sb,128)
    $n=$sb.ToString()
    if ($n.StartsWith("Raduk")) { [void]$script:all.Add(@{cls=$n;h=$x;vis=[NV2]::IsWindowVisible($x)}) }
  }
  return $true }
[void][NV2]::EnumWindows($cb,[IntPtr]::Zero)

$roles = @("RadukCaptureWindow","RadukSkyMap","RadukGoToPanel","RadukMountController",
           "RadukHelpWindow","RadukAIChat","RadukHistogram","RadukSharpness",
           "RadukTemperature","RadukStatus","RadukADCAlign","RadukShareView")
"Raduk top-level windows found by EnumThreadWindows-equivalent: {0}" -f $script:all.Count
"In kPlacedWindows roles: {0}" -f $roles.Count
"--- windows NOT covered by the old list ---"
foreach ($w in $script:all) {
  if ($roles -notcontains $w.cls) { "   {0}  visible={1}" -f $w.cls, $w.vis }
}

function MeanRGB($h) {
  $r=New-Object NV2+RECT; [void][NV2]::GetWindowRect($h,[ref]$r)
  $w=[Math]::Min(300,$r.R-$r.L); $ht=[Math]::Min(40,$r.B-$r.T)
  if ($w -le 0 -or $ht -le 0) { return $null }
  $bmp=New-Object System.Drawing.Bitmap $w,$ht
  $g=[System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size)
  $rs=0;$gs=0;$bs=0;$n=0
  for($y=0;$y -lt $ht;$y+=4){for($x=0;$x -lt $w;$x+=6){$c=$bmp.GetPixel($x,$y);$rs+=$c.R;$gs+=$c.G;$bs+=$c.B;$n++}}
  $g.Dispose();$bmp.Dispose()
  return @{R=[int]($rs/$n);G=[int]($gs/$n);B=[int]($bs/$n)}
}
$main = ($script:all | Where-Object { $_.cls -eq "RadukCaptureWindow" })[0].h
$stat = ($script:all | Where-Object { $_.cls -eq "RadukStatus" })[0].h
$before_m = MeanRGB $main; $before_s = MeanRGB $stat
[void][NV2]::SetForegroundWindow($main); Start-Sleep -Milliseconds 600
[NV2]::keybd_event(0x4E,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 80; [NV2]::keybd_event(0x4E,0,2,[IntPtr]::Zero)
Start-Sleep -Seconds 3
$after_m = MeanRGB $main; $after_s = MeanRGB $stat
"--- night vision toggle (R should rise, G and B fall) ---"
"main   before R={0} G={1} B={2}   after R={3} G={4} B={5}" -f $before_m.R,$before_m.G,$before_m.B,$after_m.R,$after_m.G,$after_m.B
"status before R={0} G={1} B={2}   after R={3} G={4} B={5}" -f $before_s.R,$before_s.G,$before_s.B,$after_s.R,$after_s.G,$after_s.B
