param(
    [string]$BinDir = 'build\bin',
    [string]$Dll = '',
    [string]$TargetExe = 'inject_target.exe',
    [string]$TargetName = 'inject_target',
    [string]$GuiExe = 'OpenInjector.exe',
    [int]$WaitSeconds = 10
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($BinDir)) { $BinDir = Join-Path $root $BinDir }
$out = Join-Path $root 'out'
if (-not (Test-Path $out)) { New-Item -ItemType Directory -Path $out | Out-Null }
if ([string]::IsNullOrWhiteSpace($Dll)) { $Dll = Join-Path $BinDir 'SamplePayload.dll' }
if (-not [System.IO.Path]::IsPathRooted($TargetExe)) { $TargetExe = Join-Path $BinDir $TargetExe }
$targetFilter = "$TargetName.exe"
$guiProcessName = [System.IO.Path]::GetFileNameWithoutExtension($GuiExe)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class GuiNative {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
}
'@

function Click-At([IntPtr]$hwnd, [float]$dipX, [float]$dipY) {
    $scale = [GuiNative]::GetDpiForWindow($hwnd) / 96.0
    if ($scale -le 0) { $scale = 1.0 }
    $x = [int][Math]::Round($dipX * $scale)
    $y = [int][Math]::Round($dipY * $scale)
    $lp = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
    [GuiNative]::PostMessage($hwnd, 0x0200, [IntPtr]::Zero, $lp) | Out-Null
    [GuiNative]::PostMessage($hwnd, 0x0201, [IntPtr]1, $lp) | Out-Null
    [GuiNative]::PostMessage($hwnd, 0x0202, [IntPtr]::Zero, $lp) | Out-Null
    Start-Sleep -Milliseconds 220
}

function Send-Text([IntPtr]$hwnd, [string]$text) {
    foreach ($ch in $text.ToCharArray()) {
        [GuiNative]::PostMessage($hwnd, 0x0102, [IntPtr][int]$ch, [IntPtr]::Zero) | Out-Null
        Start-Sleep -Milliseconds 6
    }
    Start-Sleep -Milliseconds 350
}

function Find-GuiWindow([int]$processId) {
    for ($i = 0; $i -lt 60; $i++) {
        $proc = Get-Process -Id $processId -ErrorAction SilentlyContinue
        if ($proc -and $proc.MainWindowHandle -ne 0) { return $proc.MainWindowHandle }
        Start-Sleep -Milliseconds 100
    }
    throw 'GUI main window did not appear'
}

try {
    Get-Process inject_target,inject_target32 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Get-Process OpenInjector,OpenInjector32 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
} catch {
    Write-Host "Cleanup warning: $($_.Exception.Message)"
}
Start-Sleep -Milliseconds 600
Remove-Item (Join-Path $BinDir 'payload_loaded*.txt') -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $BinDir 'payload_unloaded*.txt') -Force -ErrorAction SilentlyContinue

$target = Start-Process -FilePath $TargetExe -WorkingDirectory $BinDir -PassThru
Start-Sleep -Milliseconds 300
$targetPid = (Get-Process $TargetName).Id
Write-Host "Target PID: $targetPid ($TargetName)"

$gui = Start-Process -FilePath (Join-Path $BinDir $GuiExe) -WorkingDirectory $BinDir -PassThru
$hwnd = Find-GuiWindow $gui.Id
[GuiNative]::ShowWindow($hwnd, 9) | Out-Null
[GuiNative]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 800
Write-Host "GUI hwnd: $hwnd"

Click-At $hwnd 500 212
Send-Text $hwnd $targetFilter
Click-At $hwnd 420 277
Write-Host 'Target selected'

Click-At $hwnd 950 224
Send-Text $hwnd $Dll
Write-Host "DLL typed: $Dll"

Click-At $hwnd 940 453
$loaded = Join-Path $BinDir "payload_loaded_$targetPid.txt"
$deadline = (Get-Date).AddSeconds($WaitSeconds)
while ((Get-Date) -lt $deadline -and -not (Test-Path $loaded)) { Start-Sleep -Milliseconds 200 }
if (-not (Test-Path $loaded)) {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'capture.ps1') -ProcessName $guiProcessName -Output 'out\gui_inject_failed.png'
    throw "Inject failed: marker not found: $loaded"
}
Start-Sleep -Milliseconds 600
Write-Host 'Inject marker: OK'
powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'capture.ps1') -ProcessName $guiProcessName -Output 'out\gui_inject.png' | Out-Null

Click-At $hwnd 1100 453
$unloaded = Join-Path $BinDir "payload_unloaded_$targetPid.txt"
$deadline = (Get-Date).AddSeconds($WaitSeconds)
while ((Get-Date) -lt $deadline -and -not (Test-Path $unloaded)) { Start-Sleep -Milliseconds 200 }
if (-not (Test-Path $unloaded)) {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'capture.ps1') -ProcessName $guiProcessName -Output 'out\gui_eject_failed.png'
    throw "Eject failed: marker not found: $unloaded"
}
Start-Sleep -Milliseconds 600
Write-Host 'Eject marker: OK'
powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'capture.ps1') -ProcessName $guiProcessName -Output 'out\gui_eject.png' | Out-Null

Write-Host 'GUI smoke test: PASS'
