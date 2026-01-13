#!/bin/bash

set -euo pipefail

echo "🔧 Qt6 WebEngine 构建修复脚本"
echo "适用于：macOS + Docker + reMarkable SDK"
echo ""

HOST_GCC="/usr/bin/gcc"
HOST_GXX="/usr/bin/g++"
HOST_LD="/usr/bin/g++"
HOST_AR="/usr/bin/ar"
HOST_NM="/usr/bin/nm"
HOST_CPPFLAGS="-isystem /usr/include/c++/11 -isystem /usr/include/x86_64-linux-gnu/c++/11 -isystem /usr/include"

write_host_build_gn() {
cat <<'INNER_EOF'
import("//build/config/sysroot.gni")
import("//build/toolchain/gcc_toolchain.gni")

gcc_toolchain("host") {
  cc = "/usr/bin/gcc"
  cxx = "/usr/bin/g++"
  ld = "/usr/bin/g++"
  ar = "/usr/bin/ar"
  nm = "/usr/bin/nm"
  toolchain_args = {
    current_os = "linux"
    current_cpu = "x64"
    v8_current_cpu = "x64"
    is_clang = false
    is_mingw = false
    use_gold = false
  }
}
INNER_EOF
}

write_v8_build_gn() {
cat <<'INNER_EOF'
import("//build/toolchain/gcc_toolchain.gni")

gcc_toolchain("v8") {
  cc = "/usr/bin/gcc"
  cxx = "/usr/bin/g++"
  ld = "/usr/bin/g++"
  ar = "/usr/bin/ar"
  readelf = "readelf"
  nm = "/usr/bin/nm"

  toolchain_args = {
    current_os = "linux"
    current_cpu = "x64"
    v8_current_cpu = "x64"
    is_clang = false
    is_mingw = false
    use_gold = false
  }
}
INNER_EOF
}

# 1. 修复 host_toolchain/BUILD.gn
echo "1️⃣ 修复 host_toolchain/BUILD.gn..."
host_files=$(find src -path "*host_toolchain/BUILD.gn" 2>/dev/null || true)
if [ -n "${host_files}" ]; then
  while IFS= read -r file; do
    [ -n "$file" ] || continue
    write_host_build_gn > "$file"
    echo "✅ 已修复 $file"
  done <<< "${host_files}"
else
  echo "ℹ️ 未找到 host_toolchain/BUILD.gn，可能尚未生成"
fi

# 2. 修复 CMake 变量（如果需要）
echo ""
echo "2️⃣ 检查 CMake 变量..."
if [ -f "CMakeCache.txt" ]; then
  sed -i 's/Gn_FOUND:BOOL=.*/Gn_FOUND:BOOL=ON/' CMakeCache.txt
  sed -i 's/TEST_khr:BOOL=.*/TEST_khr:BOOL=ON/' CMakeCache.txt
  echo "✅ CMake 变量已修复"
else
  echo "ℹ️ CMakeCache.txt 不存在，跳过"
fi

# 3. 强制 CMakeLists 设置
echo ""
echo "3️⃣ 检查 QtWebEngine CMakeLists.txt..."
if [ -f "src/core/qtwebengine/src/CMakeLists.txt" ]; then
  if ! grep -q "set(TEST_khr ON" src/core/qtwebengine/src/CMakeLists.txt; then
    sed -i '/^cmake_minimum_required/a\set(TEST_khr ON CACHE BOOL "Force KHR test to ON" FORCE)' src/core/qtwebengine/src/CMakeLists.txt
  fi
  if ! grep -q "set(Gn_FOUND ON" src/core/qtwebengine/src/CMakeLists.txt; then
    sed -i '/^cmake_minimum_required/a\set(Gn_FOUND ON CACHE BOOL "Force Gn_FOUND to ON" FORCE)' src/core/qtwebengine/src/CMakeLists.txt
  fi
  echo "✅ QtWebEngine CMakeLists.txt 已修复"
else
  echo "ℹ️ QtWebEngine CMakeLists.txt 不存在，跳过"
fi

# 4. 修复 v8/host toolchain GN 定义
echo ""
echo "4️⃣ 修复 v8 toolchain BUILD.gn..."
v8_files=$(find src -path "*v8_toolchain/BUILD.gn" 2>/dev/null || true)
if [ -n "${v8_files}" ]; then
  while IFS= read -r file; do
    [ -n "$file" ] || continue
    write_v8_build_gn > "$file"
    echo "✅ 已修复 $file"
  done <<< "${v8_files}"
else
  echo "ℹ️ 未找到 v8_toolchain/BUILD.gn，可能尚未生成"
fi

echo ""
echo "🎉 修复脚本执行完成！"
echo ""
echo "使用方法："
echo "  cd build-qtwebengine"
echo "  ../fix-qtwebengine-toolchain.sh"
echo "  ninja QtWebEngineCore QtWebEngineQuick QtPdf"
