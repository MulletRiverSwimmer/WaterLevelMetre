param(
  [string]$FlowPath = "flows.json"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $FlowPath)) {
  throw "Flow file not found: $FlowPath"
}

$raw = Get-Content -LiteralPath $FlowPath -Raw

try {
  $flow = $raw | ConvertFrom-Json
} catch {
  throw "Invalid JSON in ${FlowPath}: $($_.Exception.Message)"
}

if (-not ($flow -is [System.Array])) {
  throw "Expected top-level JSON array in $FlowPath"
}

$mqttInNodes = @($flow | Where-Object { $_.type -eq "mqtt in" })
$topicsFound = @($mqttInNodes | ForEach-Object { $_.topic })

$requiredTopics = @(
  "waterlevel/+/data",
  "waterlevel/+/settings",
  "waterlevel/+/config/ack"
)

$missingTopics = @($requiredTopics | Where-Object { $_ -notin $topicsFound })
if ($missingTopics.Count -gt 0) {
  throw "Missing required mqtt in topics: $($missingTopics -join ', ')"
}

$buildConfigNode = $flow | Where-Object { $_.name -eq "Build Config" -and $_.type -eq "function" } | Select-Object -First 1
if (-not $buildConfigNode) {
  throw "Missing function node named 'Build Config'"
}

$funcCode = [string]$buildConfigNode.func
if ($funcCode -notmatch "/config/set") {
  throw "Build Config function does not route to /config/set"
}
if ($funcCode -notmatch "/config/get") {
  throw "Build Config function does not route to /config/get"
}

Write-Host "Flow validation passed: $FlowPath"
Write-Host "Found mqtt in topics:"
$requiredTopics | ForEach-Object { Write-Host " - $_" }
