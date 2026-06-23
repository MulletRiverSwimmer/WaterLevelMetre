#!/usr/bin/env powershell
<#
.SYNOPSIS
    Auto-increments firmware version in app_version.h with build date
    
.DESCRIPTION
    Updates the patch version (1.0.X) and sets the build date to today
    Intended to be run before compilation during CI/CD or manual deployment
#>

$ErrorActionPreference = "Stop"

$versionFile = "include/app_version.h"

if (-not (Test-Path $versionFile)) {
    Write-Error "Version file not found: $versionFile"
    exit 1
}

$content = Get-Content $versionFile -Raw

# Parse current version: "#define FIRMWARE_VERSION "1.0.X""
if ($content -match '#define FIRMWARE_VERSION "(\d+)\.(\d+)\.(\d+)"') {
    $major = [int]$matches[1]
    $minor = [int]$matches[2]
    $patch = [int]$matches[3]
    
    # Increment patch version
    $newPatch = $patch + 1
    $newVersion = "$major.$minor.$newPatch"
    
    Write-Host "Incrementing firmware version: $major.$minor.$patch -> $newVersion" -ForegroundColor Green
    
    # Get today's date in YYYY-MM-DD format
    $today = (Get-Date).ToString("yyyy-MM-dd")
    
    # Update version file
    $newContent = $content `
        -replace '#define FIRMWARE_VERSION "\d+\.\d+\.\d+"', "#define FIRMWARE_VERSION ""$newVersion""" `
        -replace '#define FIRMWARE_BUILD_DATE ".*?"', "#define FIRMWARE_BUILD_DATE ""$today"""
    
    Set-Content $versionFile $newContent -NoNewline
    
    Write-Host "Updated version to: $newVersion (built $today)" -ForegroundColor Green
    exit 0
} else {
    Write-Error "Could not parse version from $versionFile"
    exit 1
}
