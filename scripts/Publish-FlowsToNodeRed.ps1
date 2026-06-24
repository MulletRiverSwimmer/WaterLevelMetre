#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Validate, commit, push, and deploy flows.json to Node-RED container in one step.

.DESCRIPTION
    This script:
    1. Validates flows.json syntax and MQTT topics
    2. Stages and commits flows.json
    3. Pushes to GitHub (main branch)
    4. Deploys to remote Node-RED container via SSH

.PARAMETER Message
    Commit message (default: "Update flows")

.PARAMETER RemoteUser
    SSH user (default: "clyde@ourhome.local")

.PARAMETER RemoteHost
    SSH host (default: "VMCentos00.ourhome.local")

.PARAMETER RemoteRepoPath
    Path on remote host (default: "/home/clyde@ourhome.local/WaterLevelMetre")

.PARAMETER ContainerName
    Node-RED container name (default: "nodered")

.EXAMPLE
    ./Publish-FlowsToNodeRed.ps1 "Update dashboard version"

.EXAMPLE
    ./Publish-FlowsToNodeRed.ps1
#>

param(
    [string]$Message = "Update flows",
    [string]$RemoteUser = "clyde@ourhome.local",
    [string]$RemoteHost = "VMCentos00.ourhome.local",
    [string]$RemoteRepoPath = "/home/clyde@ourhome.local/WaterLevelMetre",
    [string]$ContainerName = "nodered"
)

$ErrorActionPreference = "Stop"

# Auto-bump version
Write-Host "=== Auto-bumping dashboard version ===" -ForegroundColor Cyan
$flowsContent = Get-Content -Path "flows.json" -Raw
$versionPattern = 'Dashboard build: (\d{4}-\d{2}-\d{2})\.(\d+)'
$match = [regex]::Match($flowsContent, $versionPattern)

if ($match.Success) {
    $dateStr = $match.Groups[1].Value
    $patchNum = [int]$match.Groups[2].Value
    $newPatch = $patchNum + 1
    $newVersion = "$dateStr.$newPatch"
    $oldVersion = "$dateStr.$patchNum"
    
    $updatedContent = $flowsContent -replace "Dashboard build: $oldVersion", "Dashboard build: $newVersion"
    Set-Content -Path "flows.json" -Value $updatedContent
    Write-Host "Version bumped: $oldVersion → $newVersion" -ForegroundColor Green
} else {
    Write-Host "Warning: Could not find version pattern in flows.json" -ForegroundColor Yellow
}

Write-Host "`n=== Validating flows.json ===" -ForegroundColor Cyan
 $global:LASTEXITCODE = 0
& ".\scripts\validate-flow.ps1"
if (-not $? -or $LASTEXITCODE -ne 0) {
    Write-Host "Validation failed!" -ForegroundColor Red
    exit 1
}

Write-Host "`n=== Committing to local repo ===" -ForegroundColor Cyan
& 'C:\Program Files\Git\cmd\git.exe' add flows.json
& 'C:\Program Files\Git\cmd\git.exe' commit -m "$Message"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Commit failed or no changes!" -ForegroundColor Red
    exit 1
}

Write-Host "`n=== Pushing to GitHub ===" -ForegroundColor Cyan
& 'C:\Program Files\Git\cmd\git.exe' push
if ($LASTEXITCODE -ne 0) {
    Write-Host "Push failed!" -ForegroundColor Red
    exit 1
}

Write-Host "`n=== Deploying to Node-RED container ===" -ForegroundColor Cyan
$sshCmd = "cd '$RemoteRepoPath' && bash ./scripts/deploy-centos.sh '$RemoteRepoPath' main $ContainerName"
& ssh -tt -l "$RemoteUser" "$RemoteHost" "$sshCmd"
if ($LASTEXITCODE -ne 0) {
    Write-Host "SSH deploy failed!" -ForegroundColor Red
    exit 1
}

Write-Host "`n=== Deployment complete ===" -ForegroundColor Green
Write-Host "Next: Hard-refresh dashboard (Ctrl+F5)" -ForegroundColor Yellow
