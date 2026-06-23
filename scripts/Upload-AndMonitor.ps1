#!/usr/bin/env powershell
<#
.SYNOPSIS
    Uploads firmware and manages serial monitor automatically.
    Stops monitor before upload, restarts after completion.
    Auto-detects upload port from platformio.ini or uses PlatformIO's auto-detection.

.PARAMETER Port
    Serial port (optional - uses platformio.ini or auto-detection if not specified)

.PARAMETER NoRestart
    If specified, don't restart the monitor after upload
#>

param(
    [string]$Port = "",
    [switch]$NoRestart
)

$ErrorActionPreference = "Stop"

# Color output
function Write-Success { Write-Host $args -ForegroundColor Green }
function Write-Error_ { Write-Host $args -ForegroundColor Red }
function Write-Info { Write-Host $args -ForegroundColor Cyan }

Write-Info "[UPLOAD] Starting firmware upload with monitor control..."

# Bump firmware version
Write-Info "[VERSION] Bumping firmware version..."
try {
    & (Join-Path $PSScriptRoot "Bump-FirmwareVersion.ps1")
} catch {
    Write-Error_ "[VERSION] Failed to bump version: $_"
    exit 1
}

# Get PlatformIO path
$platformio = $null
$explicitPath = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"
if (Test-Path $explicitPath) {
    $platformio = $explicitPath
} elseif ($env:PLATFORMIO_EXE -and (Test-Path $env:PLATFORMIO_EXE)) {
    $platformio = $env:PLATFORMIO_EXE
} else {
    $platformio = "platformio.exe"
}

Write-Info "[UPLOAD] Using PlatformIO: $platformio"

# If port not specified, try to get it from platformio.ini
if (-not $Port) {
    Write-Info "[PORT] Detecting upload port from platformio.ini or auto-detection..."
    
    # Try to read upload_port from platformio.ini
    $iniPath = Join-Path (Get-Location) "platformio.ini"
    if (Test-Path $iniPath) {
        $iniContent = Get-Content $iniPath -Raw
        if ($iniContent -match 'upload_port\s*=\s*([^\s\n]+)') {
            $Port = $matches[1].Trim()
            Write-Info "[PORT] Found in platformio.ini: $Port"
        } else {
            Write-Info "[PORT] No upload_port in platformio.ini; using PlatformIO auto-detection"
        }
    } else {
        Write-Info "[PORT] No platformio.ini found; using PlatformIO auto-detection"
    }
}

if ($Port) {
    Write-Info "[PORT] Using configured port: $Port"
} else {
    Write-Info "[PORT] PlatformIO will auto-detect the upload port"
}

# Check if monitor is running for this port
if ($Port) {
    Write-Info "[MONITOR] Checking for active serial monitor on $Port..."
    $monitorSearchPattern = $Port
} else {
    Write-Info "[MONITOR] Checking for active serial monitor (port auto-detection)..."
    $monitorSearchPattern = "COM|ttyUSB|ttyACM"  # Generic pattern
}

$monitorProcesses = @()

try {
    # Try to find PlatformIO monitor processes
    $allProcesses = Get-Process "python*" -ErrorAction SilentlyContinue
    foreach ($proc in $allProcesses) {
        try {
            $cmdLine = (Get-CimInstance Win32_Process -Filter "ProcessId = $($proc.Id)" -ErrorAction SilentlyContinue).CommandLine
            if ($cmdLine -and $cmdLine -match "monitor" -and $cmdLine -match $monitorSearchPattern) {
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

# Build upload command
$uploadCmd = "$platformio run --target upload"
if ($Port) {
    $uploadCmd += " --upload-port $Port"
}

# Run upload
Write-Info "[UPLOAD] Running: $uploadCmd"
Write-Info "==============================================="

Invoke-Expression $uploadCmd
$uploadResult = $LASTEXITCODE

Write-Info "==============================================="

if ($uploadResult -eq 0) {
    Write-Success "[UPLOAD] Upload completed successfully!"
} else {
    Write-Error_ "[UPLOAD] Upload failed with exit code $uploadResult"
    exit $uploadResult
}

# For monitor restart, we need to know which port was actually used
# If Port was specified, use it; otherwise try to detect from recent uploads
if (-not $Port) {
    Write-Info "[PORT] Attempting to detect which port was used for upload..."
    # Try to get from platformio device list
    & $platformio device list 2>$null | ForEach-Object {
        if ($_ -match "^(/dev/|COM)\S+") {
            $Port = $matches[0]
            Write-Info "[PORT] Detected: $Port"
        }
    }
}

# Restart monitor if it was running before
if ($monitorWasRunning -and -not $NoRestart) {
    if ($Port) {
        Write-Info "[MONITOR] Restarting serial monitor on $Port..."
        Start-Sleep -Seconds 2
        & $platformio device monitor --port $Port --baud 115200
    } else {
        Write-Info "[MONITOR] Monitor auto-detection not available; skipping restart"
    }
} else {
    Write-Info "[MONITOR] Monitor restart skipped (was not running or --NoRestart specified)"
}

Write-Success "[UPLOAD] Done!"
