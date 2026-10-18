param([int]$Seconds = 15)
Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices;
public class RT {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
}
'@
$script:p2=(Get-Process RadukCapture | Select-Object -First 1).Id
$script:main=[IntPtr]::Zero
$cb=[RT+EnumProc]{ param($x,$l)
  $p=0; [void][RT]::GetWindowThreadProcessId($x,[ref]$p)
  if ($p -eq $script:p2) { $sb=New-Object System.Text.StringBuilder 128; [void][RT]::GetClassName($x,$sb,128)
    if ($sb.ToString() -eq "RadukCaptureWindow") { $script:main=$x } }
  return $true }
[void][RT]::EnumWindows($cb,[IntPtr]::Zero)
if ($script:main -eq [IntPtr]::Zero) { "main window not found"; exit 1 }
$script:panel=[IntPtr]::Zero
$ccb=[RT+EnumProc]{ param($c,$l)
  $sb=New-Object System.Text.StringBuilder 128; [void][RT]::GetClassName($c,$sb,128)
  if ($sb.ToString() -eq "RadukControlPanel") { $script:panel=$c }
  return $true }
[void][RT]::EnumChildWindows($script:main,$ccb,[IntPtr]::Zero)
if ($script:panel -eq [IntPtr]::Zero) { "control panel not found"; exit 1 }
"control panel found: $($script:panel)"

$dir = "D:\RadukCapture\captures"
$before = @(Get-ChildItem $dir -Filter *.ser -ErrorAction SilentlyContinue)
"ser files before: $($before.Count)"

[void][RT]::SendMessage($script:panel, 0x0111, [IntPtr]1800, [IntPtr]0)   # WM_COMMAND, RECORD
"RECORD pressed; running $Seconds s"
Start-Sleep -Seconds $Seconds
[void][RT]::SendMessage($script:panel, 0x0111, [IntPtr]1800, [IntPtr]0)   # stop
"STOP pressed"
Start-Sleep -Seconds 6

$after = @(Get-ChildItem $dir -Filter *.ser -ErrorAction SilentlyContinue)
"ser files after:  $($after.Count)"
$new = $after | Where-Object { $before.Name -notcontains $_.Name } | Sort-Object LastWriteTime -Descending
foreach ($f in $new) {
  $mb = $_.Length
  "NEW: {0}" -f $f.Name
  "     {0:N1} MB" -f ($f.Length/1MB)
}
