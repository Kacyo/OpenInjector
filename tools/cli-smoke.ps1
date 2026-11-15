param(
    [string]$BinDir = 'build\bin',
    [string]$TargetExe = 'inject_target.exe',
    [string]$TargetName = 'inject_target',
    [string]$Dll = 'SamplePayload.dll',
    [string]$ToolExe = 'OpenInjectorTool.exe',
    [int]$WaitSeconds = 10
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($BinDir)) { $BinDir = Join-Path $root $BinDir }

$tool = Join-Path $BinDir $ToolExe
if (-not [System.IO.Path]::IsPathRooted($TargetExe)) { $TargetExe = Join-Path $BinDir $TargetExe }
if (-not [System.IO.Path]::IsPathRooted($Dll)) { $Dll = Join-Path $BinDir $Dll }

if (-not (Test-Path $tool)) { throw "CLI not found: $tool" }

function Wait-Marker([string]$path) {
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $path) { return $true }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

function Clean-Markers {
    Remove-Item (Join-Path $BinDir 'payload_loaded*.txt') -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $BinDir 'payload_unloaded*.txt') -Force -ErrorAction SilentlyContinue
}

try {
    Get-Process inject_target,inject_target32 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
} catch { }

Clean-Markers
$target = Start-Process -FilePath $TargetExe -WorkingDirectory $BinDir -PassThru
Start-Sleep -Milliseconds 300
$pidValue = (Get-Process $TargetName).Id
Write-Host "Target PID: $pidValue ($TargetName)"

$failed = $false

foreach ($method in @('crt', 'nt', 'apc')) {
    Clean-Markers
    & $tool --inject $Dll --pid $pidValue --method $method --timeout ($WaitSeconds * 1000) --quiet
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Method $method : inject FAILED (exit $LASTEXITCODE)"
        $failed = $true
        continue
    }
    if (-not (Wait-Marker (Join-Path $BinDir "payload_loaded_$pidValue.txt"))) {
        Write-Host "Method $method : inject marker FAILED"
        $failed = $true
        continue
    }

    & $tool --eject $Dll --pid $pidValue --quiet
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Method $method : eject FAILED (exit $LASTEXITCODE)"
        $failed = $true
        continue
    }
    if (-not (Wait-Marker (Join-Path $BinDir "payload_unloaded_$pidValue.txt"))) {
        Write-Host "Method $method : eject marker FAILED"
        $failed = $true
        continue
    }
    Write-Host "Method $method : PASS"
}

# Launch-and-inject regression.
$target | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400
Clean-Markers
& $tool --launch $TargetExe --inject $Dll --quiet
if ($LASTEXITCODE -ne 0) {
    Write-Host "Launch-and-inject: FAILED (exit $LASTEXITCODE)"
    $failed = $true
} else {
    $launched = Get-Process $TargetName -ErrorAction SilentlyContinue |
        Sort-Object StartTime -Descending | Select-Object -First 1
    $launchedPid = if ($launched) { $launched.Id } else { 0 }
    if ($launchedPid -le 0) {
        Write-Host "Launch-and-inject: FAILED (launched process not found)"
        $failed = $true
    } elseif (-not (Wait-Marker (Join-Path $BinDir "payload_loaded_$launchedPid.txt"))) {
        Write-Host "Launch-and-inject: FAILED (marker missing, pid $launchedPid)"
        $failed = $true
    } else {
        & $tool --eject $Dll --pid $launchedPid --quiet
        if (-not (Wait-Marker (Join-Path $BinDir "payload_unloaded_$launchedPid.txt"))) {
            Write-Host "Launch-and-inject: eject marker FAILED"
            $failed = $true
        } else {
            Write-Host "Launch-and-inject: PASS (pid $launchedPid)"
        }
        Get-Process -Id $launchedPid -ErrorAction SilentlyContinue | Stop-Process -Force
    }
}

# Error handling: nonexistent PID must fail.
$bad = Start-Process -FilePath $tool -ArgumentList @('--inject', ('"' + $Dll + '"'), '--pid', '999999', '--quiet') -Wait -PassThru
if ($bad.ExitCode -eq 0) {
    Write-Host 'Invalid PID check: FAILED (accepted nonexistent PID)'
    $failed = $true
} else {
    Write-Host "Invalid PID check: PASS (exit $($bad.ExitCode))"
}

try {
    Get-Process inject_target,inject_target32 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
} catch { }

if ($failed) {
    Write-Host 'CLI smoke test: FAIL'
    exit 1
}
Write-Host 'CLI smoke test: PASS'
