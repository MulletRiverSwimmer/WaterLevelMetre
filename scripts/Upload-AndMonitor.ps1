#!/usr/bin/env powershell
<#
.SYNOPSIS
    Uploads firmware and manages serial monitor automatically.
    Stops monitor before upload, restarts after completion.

.PARAMETER Port
    Serial port (default: COM8)

.PARAMETER NoRestart
    If specified, don't restart the monitor after upload
#>

param(
    [string]$Port = "COM8",
    [switch]$NoRestart
)

$ErrorActionPreference = "Stop"

# Color output
function Write-Success { Write-Host $args -ForegroundColor Green }
function Write-Error_ { Write-Host $args -ForegroundColor Red }
function Write-Info { Write-Host $args -ForegroundColor Cyan }

Write-Info "[UPLOAD] Starting firmware upload with monitor control..."

# Get PlatformIO path
$platformio = & {
    try {
        $env:PLATFORMIO_EXE
    } catch {
        join-path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"
    }
}

if (-not (Test-Path $platformio)) {
    $platformio = "platformio"
}

# Check if monitor is running for this port
Write-Info "[MONITOR] Checking for active serial monitor on $Port..."
$monitorProcesses = @()

try {
    # Try to find PlatformIO monitor processes
    $allProcesses = Get-Process "python*" -ErrorAction SilentlyContinue
    foreach ($proc in $allProcesses) {
        try {
            $cmdLine = (Get-CimInstance Win32_Process -Filter "ProcessId = $($proc.Id)" -ErrorAction SilentlyContinue).CommandLine
            if ($cmdLine -and $cmdLine -match "monitor" -and $cmdLine -match $Port) {
                $monitorProcesses += $proc
                Write-Info "[MONITOR] Found PlatformIO monitor process (PID: $($proc.Id))"
            }
        } catch {
            # Ignore errors getting process info
        }
    }
} catch {
    Write-Info "[MONITOR] Could not enumerate processes"
}

# Stop monitor if found
$monitorWasRunning = $monitorProcesses.Count -gt 0
if ($monitorWasRunning) {
    Write-Info "[MONITOR] Stopping serial monitor..."
    foreach ($proc in $monitorProcesses) {
        try {
            $proc | Stop-Process -Force -ErrorAction SilentlyContinue
            Write-Success "[MONITOR] Stopped PID $($proc.Id)"
            Start-Sleep -Milliseconds 500
        } catch {
            # Process may already be stopped
        }
    }
    Start-Sleep -Seconds 1
} else {
    Write-Info "[MONITOR] No active monitor found"
}

# Run upload
Write-Info "[UPLOAD] Running: $platformio run --target upload --upload-port $Port"
Write-Info "==============================================="

& $platformio run --target upload --upload-port $Port
$uploadResult = $LASTEXITCODE

Write-Info "==============================================="

if ($uploadResult -eq 0) {
    Write-Success "[UPLOAD] Upload completed successfully!"
} else {
    Write-Error_ "[UPLOAD] Upload failed with exit code $uploadResult"
    exit $uploadResult
}

# Restart monitor if it was running before
if ($monitorWasRunning -and -not $NoRestart) {
    Write-Info "[MONITOR] Restarting serial monitor..."
    Start-Sleep -Seconds 2
    
    & $platformio device monitor --port $Port --baud 115200
} else {
    Write-Info "[MONITOR] Monitor restart skipped (was not running or --NoRestart specified)"
}

Write-Success "[UPLOAD] Done!"
