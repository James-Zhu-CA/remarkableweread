# Qt6 WebEngine 构建问题诊断指南

## 问题分层识别

### 1. GN 层面问题
**特征**：
- 日志出现：`ERROR at //...`
- 日志出现：`Unable to load ".../BUILD.gn"`
- GN Done 阶段失败

**可能原因**：
- qtwebengine_target 路径格式错误
- GN root 目录设置问题
- BUILD.gn 文件缺失或路径错误

**解决方案**：
- 检查 args.gn 中的 qtwebengine_target 格式
- 确认 CMake 正确设置了 GN root
- 对比 PDF 和 Core 的 args.gn 差异

### 2. Host 工具链问题
**特征**：
- 错误路径：`host/obj/...`
- 编译器错误：交叉编译器用于 host 代码
- 头文件找不到：`<cmath>`, `<string>`, `<atomic>`

**可能原因**：
- host_toolchain/BUILD.gn 配置错误
- 缺少 -isystem 标准库路径
- 交叉编译器污染了 host 环境

**解决方案**：
- 修改 host_toolchain/BUILD.gn 使用 `/usr/bin/g++`
- 添加 `-isystem /usr/include/c++/11` 等路径
- 运行 fix-qtwebengine-toolchain.sh

### 3. Target (ARM64) 编译问题
**特征**：
- 错误路径：`obj/...` 或 `./qtwebengine/src/...`
- 编译器：aarch64-remarkable-linux-g++
- 链接错误或依赖问题

**可能原因**：
- 交叉编译环境配置问题
- 依赖库版本不匹配
- sysroot 路径错误

**解决方案**：
- 检查 remarkable-aarch64.cmake
- 确认 SDK 环境变量正确
- 检查 pkg-config 路径

## 构建流程标准化

### 1. 从零重建
```bash
# 清理
./clean_and_configure.sh

# 构建  
cd build-qtwebengine
./build_webengine.sh
```

### 2. 修复现有构建
```bash
cd build-qtwebengine
../fix-qtwebengine-toolchain.sh
ninja QtWebEngineCore QtWebEngineQuick QtPdf
```

### 3. 验证构建成功
```bash
# 检查库文件
find build-qtwebengine -name "libQt6WebEngine*.so"
find build-qtwebengine -name "libQt6Pdf*.so"

# 检查构建统计
find build-qtwebengine/src/core/Release/aarch64 -name "*.so" | wc -l
```

## 常见错误及解决

### 错误：`fatal error: cmath: No such file or directory`
**原因**：host 工具链使用交叉编译器
**解决**：运行 fix-qtwebengine-toolchain.sh

### 错误：`Unable to load ".../BUILD.gn"`
**原因**：qtwebengine_target 路径格式错误
**解决**：检查 args.gn 中的路径格式

### 错误：`build.ninja:18: loading 'toolchain.ninja': No such file or directory`
**原因**：GN 配置不完整
**解决**：重新运行 CMake 配置

### 错误：`Gn gen failed`
**原因**：GN 版本或路径问题
**解决**：确认 GN_EXECUTABLE 和 Gn_FOUND 设置

## 构建产物说明

- `libQt6WebEngineCore.so`：核心 Web 引擎库
- `libQt6WebEngineQuick.so`：Qt Quick 集成库
- `libQt6Pdf.so`：PDF 渲染库
- `v8/torque`：V8 JavaScript 引擎工具
- 各种 `.o` 文件：编译中间产物
- `*.a` 文件：静态库中间产物

## 性能优化建议

1. **并行编译**：使用 `ninja -j$(nproc)`
2. **增量构建**：修改代码后只需重新编译修改部分
3. **磁盘空间**：确保有 20GB+ 可用空间
4. **内存**：推荐 8GB+ RAM

## 集成到应用

构建成功后，可以：
1. 复制库文件到设备
2. 创建 Qt6 Web 应用
3. 实现 WebView 组件
4. 部署到 reMarkable
