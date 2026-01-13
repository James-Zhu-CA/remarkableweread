#!/bin/bash

echo "🧹 从零开始重建 Qt6 WebEngine"
echo "适用于：macOS + Docker + reMarkable SDK"
echo ""

# 1. 清理旧构建
echo "1️⃣ 清理旧构建..."
if [ -d "build-qtwebengine" ]; then
    echo "删除旧的 build-qtwebengine 目录..."
    rm -rf build-qtwebengine
fi

# 2. 创建新构建目录
echo ""
echo "2️⃣ 创建新构建目录..."
mkdir -p build-qtwebengine
cd build-qtwebengine

# 3. CMake 配置
echo ""
echo "3️⃣ CMake 配置..."
source /opt/remarkable-sdk/environment-setup-cortexa55-remarkable-linux
cmake -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/opt/remarkable/qt6 \
  -DCMAKE_TOOLCHAIN_FILE=/workspace/toolchains/remarkable-aarch64.cmake \
  -DFEATURE_opengl=OFF \
  -DFEATURE_opengles2=OFF \
  -DFEATURE_egl=OFF \
  -DFEATURE_xcb=OFF \
  -DFEATURE_brotli=OFF \
  -DINPUT_opengl=no \
  -DCMAKE_DISABLE_FIND_PACKAGE_OpenGL=TRUE \
  -DCMAKE_DISABLE_FIND_PACKAGE_EGL=TRUE \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DHAVE_DASH_DASH_NO_UNDEFINED=TRUE \
  -DQT_BUILD_EXAMPLES=OFF \
  -DQT_BUILD_TESTS=OFF \
  -DQT_BUILD_TOOLS_WHEN_CROSSBUILDING=ON \
  -DQT_SYNC_HEADERS_AT_CONFIGURE_TIME=OFF \
  -DCMAKE_PREFIX_PATH= \
  -DQT_FORCE_FIND_TOOLS_FROM_HOST_PATH=ON \
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
  -DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF \
  -DOPENGL_DIR= \
  -DEGL_DIR= \
  ../qt-everywhere-src-6.8.2

if [ $? -ne 0 ]; then
    echo "❌ CMake 配置失败"
    exit 1
fi

# 4. 运行修复脚本
echo ""
echo "4️⃣ 运行修复脚本..."
if [ -f "../fix-qtwebengine-toolchain.sh" ]; then
    ../fix-qtwebengine-toolchain.sh
else
    echo "❌ 找不到修复脚本"
    exit 1
fi

# 5. 备份关键文件
echo ""
echo "5️⃣ 备份关键文件..."
mkdir -p _snapshot
cp CMakeCache.txt _snapshot/ 2>/dev/null || true
cp src/core/Release/aarch64/args.gn _snapshot/ 2>/dev/null || true
cp src/core/host_toolchain/BUILD.gn _snapshot/ 2>/dev/null || true

echo ""
echo "🎉 配置完成！"
echo ""
echo "构建方法："
echo "  cd build-qtwebengine"
echo "  ../build_webengine.sh"
