#!/bin/sh
# Download the exact SDL3 and OpenXR SDK versions used by the Android build.
set -e
cd "$(dirname "$0")"
mkdir -p third_party
[ -d third_party/SDL ] || git clone --depth 1 -b release-3.2.30 https://github.com/libsdl-org/SDL.git third_party/SDL
[ -d third_party/OpenXR-SDK ] || git clone --depth 1 -b release-1.1.65 https://github.com/KhronosGroup/OpenXR-SDK.git third_party/OpenXR-SDK
echo "Dependencies ready in android/third_party"
