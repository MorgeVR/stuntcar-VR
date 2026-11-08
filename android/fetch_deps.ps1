# Download the exact SDL3 and OpenXR SDK versions used by the Android build (Windows).
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force third_party | Out-Null
if (-not (Test-Path third_party/SDL)) { git clone --depth 1 -b release-3.2.30 https://github.com/libsdl-org/SDL.git third_party/SDL }
if (-not (Test-Path third_party/OpenXR-SDK)) { git clone --depth 1 -b release-1.1.65 https://github.com/KhronosGroup/OpenXR-SDK.git third_party/OpenXR-SDK }
Write-Host "Dependencies ready in android/third_party"
