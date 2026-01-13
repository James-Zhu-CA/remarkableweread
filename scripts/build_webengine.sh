#!/bin/bash

echo "🚀 构建 Qt6 WebEngine"
echo "时间：$(date)"
echo ""

LOG_FILE="qtwebengine-build-$(date +%Y%m%d-%H%M%S).log"
echo "日志文件：$LOG_FILE"
echo ""

# 构建主要组件
echo "开始构建..."
ninja QtWebEngineCore QtWebEngineQuick QtPdf 2>&1 | tee "$LOG_FILE"

# 检查构建结果
echo ""
echo "构建结果检查："
if [ -f "src/core/Release/aarch64/libQt6WebEngineCore.so" ]; then
    echo "✅ QtWebEngineCore：成功"
    ls -lh src/core/Release/aarch64/libQt6WebEngineCore.so
else
    echo "❌ QtWebEngineCore：失败"
fi

if [ -f "src/core/Release/aarch64/libQt6WebEngineQuick.so" ]; then
    echo "✅ QtWebEngineQuick：成功"
    ls -lh src/core/Release/aarch64/libQt6WebEngineQuick.so
else
    echo "❌ QtWebEngineQuick：失败"
fi

if [ -f "src/pdf/Release/aarch64/libQt6Pdf.so" ]; then
    echo "✅ QtPdf：成功" 
    ls -lh src/pdf/Release/aarch64/libQt6Pdf.so
else
    echo "❌ QtPdf：失败"
fi

echo ""
echo "构建统计："
find src/core/Release/aarch64 -name "*.o" -type f 2>/dev/null | wc -l | xargs echo "目标文件："
find src/core/Release/aarch64 -name "*.so" -type f 2>/dev/null | wc -l | xargs echo "动态库："
du -sh src/core/Release/aarch64 2>/dev/null | xargs echo "构建大小："

echo ""
echo "日志已保存到：$LOG_FILE"
