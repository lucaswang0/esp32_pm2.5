# ============================================================================
# ESP32 PM2.5 Fan - OTA Firmware Release Script
#
# Usage:
#   .\scripts\release_ota.ps1                # Build + generate firmware/ dir
#   .\scripts\release_ota.ps1 -SkipBuild     # Skip build
#   .\scripts\release_ota.ps1 -AutoPush      # Auto commit + push to gh-pages
#
# GitHub Pages layout (on gh-pages branch):
#   firmware/
#     version.json
#     pm2_fan_v20261123.bin
# ============================================================================
param(
    [switch]$SkipBuild,
    [switch]$AutoPush
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ConfigFile  = Join-Path $ProjectRoot "include\config.h"
$BuildDir    = Join-Path $ProjectRoot ".pio\build\esp32c3-supermini"
$BinFile     = Join-Path $BuildDir "firmware.bin"
$OutDir      = Join-Path $ProjectRoot "firmware"
$PioExe      = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$PagesUrl    = "https://lucaswang0.github.io/esp32_pm2.5"

# ---- 1. Read version from config.h ----
Write-Host "==> Read config.h ..." -ForegroundColor Cyan
if (-not (Test-Path $ConfigFile)) {
    Write-Error "Config not found: $ConfigFile"
    exit 1
}

$versionMatch = Select-String -Path $ConfigFile -Pattern 'FIRMWARE_VERSION\s+"([^"]+)"' -AllMatches
if (-not $versionMatch.Matches.Success) {
    Write-Error "FIRMWARE_VERSION not found in config.h"
    exit 1
}
$Version = $versionMatch.Matches[0].Groups[1].Value
Write-Host "    Version: $Version" -ForegroundColor Green

# ---- 2. Build ----
if (-not $SkipBuild) {
    Write-Host "==> Build firmware ..." -ForegroundColor Cyan
    if (-not (Test-Path $PioExe)) {
        Write-Error "pio.exe not found: $PioExe"
        exit 1
    }
    Push-Location $ProjectRoot
    & $PioExe run
    $exitCode = $LASTEXITCODE
    Pop-Location
    if ($exitCode -ne 0) {
        Write-Error "Build FAILED (exit=$exitCode)"
        exit 1
    }
} else {
    Write-Host "==> Skip build (-SkipBuild)" -ForegroundColor Yellow
}

if (-not (Test-Path $BinFile)) {
    Write-Error "Build artifact not found: $BinFile"
    exit 1
}

$binSize = (Get-Item $BinFile).Length
Write-Host "    Firmware size: $([math]::Round($binSize/1MB, 2)) MB" -ForegroundColor Green

# ---- 3. Prepare output ----
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$binName   = "pm2_fan_v$Version.bin"
$binDest   = Join-Path $OutDir $binName
$jsonDest  = Join-Path $OutDir "version.json"

if (Test-Path $binDest) { Remove-Item $binDest -Force }
Copy-Item $BinFile $binDest

# ---- 4. Generate version.json ----
$downloadUrl = "$PagesUrl/firmware/$binName"
$json = @"
{
  "version": "$Version",
  "url": "$downloadUrl",
  "size": $binSize,
  "build_time": "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
}
"@
$json | Set-Content -Path $jsonDest -Encoding UTF8

Write-Host ""
Write-Host "==> Output files" -ForegroundColor Cyan
Write-Host "    $binDest"
Write-Host "    $jsonDest"

Write-Host ""
Write-Host "==> version.json content" -ForegroundColor Cyan
Get-Content $jsonDest | ForEach-Object { Write-Host "    $_" }

Write-Host ""
Write-Host "==> Validation" -ForegroundColor Cyan
Write-Host "    OTA_CHECK_URL -> $PagesUrl/firmware/version.json" -ForegroundColor Yellow
Write-Host "    Firmware URL  -> $downloadUrl" -ForegroundColor Yellow

# ---- 5. Auto push to gh-pages ----
if ($AutoPush) {
    Write-Host ""
    Write-Host "==> Push to gh-pages branch ..." -ForegroundColor Cyan

    Push-Location $ProjectRoot

    $localBranch  = git branch --list "gh-pages" 2>$null
    $remoteBranch = git ls-remote --heads origin "gh-pages" 2>$null

    if ($localBranch -or $remoteBranch) {
        git checkout gh-pages
        git pull origin gh-pages 2>$null

        $oldBins = Get-ChildItem -Path (Join-Path $ProjectRoot "firmware") -Filter "*.bin" -ErrorAction SilentlyContinue
        if ($oldBins) { $oldBins | Remove-Item -Force }

        Copy-Item $binDest  (Join-Path $ProjectRoot "firmware\") -Force
        Copy-Item $jsonDest (Join-Path $ProjectRoot "firmware\") -Force

        git add firmware/
        git commit -m "OTA: release v$Version"
        git push origin gh-pages
        git checkout main
    } else {
        git checkout --orphan gh-pages
        git reset

        # orphan 分支会残留未跟踪文件，先清空工作区
        git clean -fd

        New-Item -ItemType Directory -Force -Path firmware | Out-Null
        Copy-Item $binDest  (Join-Path $ProjectRoot "firmware\") -Force
        Copy-Item $jsonDest (Join-Path $ProjectRoot "firmware\") -Force

        '<!doctype html><title>ESP32 PM2.5 OTA</title><p>Firmware: <a href="firmware/">firmware/</a></p>' | Set-Content index.html

        git add .
        git commit -m "OTA: initial release v$Version"
        git push -u origin gh-pages
        git checkout main
    }

    Pop-Location
    Write-Host "    OK - pushed to gh-pages" -ForegroundColor Green
    Write-Host "    Pages URL: $PagesUrl" -ForegroundColor Green
    Write-Host "    (Allow 1-2 min for Pages deploy)" -ForegroundColor Yellow
} else {
    Write-Host ""
    Write-Host "==> Manual push steps" -ForegroundColor Cyan
    Write-Host "    Option 1: run .\scripts\release_ota.ps1 -AutoPush"
    Write-Host "    Option 2: git checkout -b gh-pages; copy firmware/*; git push"
}

Write-Host ""
Write-Host "==> Done." -ForegroundColor Green
