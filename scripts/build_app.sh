#!/bin/bash
set -e

# Build script for WereadEinkBrowser
# Based on docs/critical_info.md, updated for 'src' directory

echo "🚀 Starting Build in Docker environment..."

docker run --rm -v "$PWD":/workspace -w /workspace ubuntu:22.04 bash -lc "
  set -e
  # 1. Install dependencies (Quickly if cached, but apt-get update is slow)
  echo '📦 Installing build dependencies...'
  dpkg --add-architecture arm64 
  apt-get update >/dev/null 2>&1
  apt-get install -y qemu-user-static cmake ninja-build build-essential g++-aarch64-linux-gnu \
    libpcre2-16-0:arm64 libnss3:arm64 libnspr4:arm64 libdbus-1-3:arm64 \
    libglib2.0-0:arm64 libharfbuzz0b:arm64 libxkbcommon0:arm64 \
    libwebpdemux2:arm64 libwebpmux3:arm64 libgraphite2-3:arm64 >/dev/null 2>&1

  echo '🔧 Configuring CMake...'
  # Clean build directory if needed
  # rm -rf /workspace/src/build-cross

  /workspace/qt6-src/build-qt6/qtbase/bin/qt-cmake -G Ninja \
    -S /workspace/src \
    -B /workspace/src/build-cross \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DCMAKE_PREFIX_PATH=/workspace/qt6-src/build-qt6/qtbase/lib/cmake \
    2>&1 | tail -10

  echo '🔨 Building...'
  cmake --build /workspace/src/build-cross 2>&1 | tail -20

  echo '✅ Build Complete!'
  ls -lh /workspace/src/build-cross/WereadEinkBrowser
"
