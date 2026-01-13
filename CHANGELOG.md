# 更新日志

## 2026-01-10 - 目录跳转修正

### 📝 变更
- 目录跳转基于进度接口锚点（chapterUid/chapterIdx），假设章节 UID 连续并按目录顺序映射
- 注入轻量进度监听，避免依赖完整章节列表

### 🎯 影响
- 目录点击使用当前章节锚点推算目标 UID，提高跳转一致性（在连续 UID 前提下）

## 2026-01-11 - 会话保存保护

### 📝 变更
- 会话保存过滤 `chrome-error://`，避免覆盖最后正常 URL
- 会话恢复优先使用 `lastGoodUrl`（如果存在）
- 章节进度探针仅使用 `__WR_CHAPTER_OBS`，不再读取页面状态

### 🎯 影响
- 启动不再被错误页会话卡住，优先回到最后正常页面

## 2026-01-07 - 阅读体验与 UA 调整

### 📝 变更
- 书籍页默认字体级别强制为 4（未标记用户自定义时），字体调整会标记用户自定义
- 菜单新增目录入口并接通目录组件跳转
- 非书籍页面 UA 改为 Kindle

### 🎯 影响
- 目录入口可用、默认字号一致、字号调整立即生效

## 2025-01-XX - 架构修正更新 v1.1

### 🔥 重大更新

#### 架构错误修正
- **问题**: 初始版本错误假设reMarkable Paper Pro Move使用ARMv7架构
- **实际**: 设备使用ARMv8 (aarch64)架构
- **影响**: ARMv7编译的二进制文件在设备上完全无法运行

#### 解决方案：Docker编译
- ✅ 采用Docker容器提供标准aarch64交叉编译环境
- ✅ 简化设置流程（从30分钟降至5-10分钟）
- ✅ 跨平台一致性（macOS/Linux相同流程）
- ✅ 自动验证生成的二进制架构

### 📝 修改的文件

#### 新增文件
- `src/Dockerfile` - Docker编译环境定义
- `src/.dockerignore` - Docker构建忽略文件
- `CHANGELOG.md` - 本文件

#### 重大修改
- `src/build.sh` - 重写为Docker编译方案
- `src/remarkable-toolchain.cmake` - ARMv7 → ARMv8配置
- `src/QUICKSTART.md` - 完全重写为Docker流程
- `src/README.md` - 更新交叉编译说明
- `README.md` - 更新架构说明和实施指南

### 🎯 用户影响

#### 如果您还未开始
- ✅ **好消息**: 新方案更简单更快
- ✅ 直接按照更新后的文档执行即可
- ✅ 首次时间从40-45分钟降至20-30分钟

#### 如果您已使用旧版
- ⚠️ **需要重新编译**: 旧二进制无法运行
- 步骤：安装Docker → 删除build-arm → 重新执行./build.sh

### 📊 性能对比

| 指标 | 旧方案 (ARMv7) | 新方案 (aarch64 Docker) |
|------|----------------|------------------------|
| 工具链安装 | 30分钟 | **5-10分钟** ✅ |
| 首次编译 | 2-5分钟 | 8-15分钟（含镜像） |
| 首次总时间 | 40-45分钟 | **20-30分钟** ✅ |
| 架构兼容 | ❌ 错误 | ✅ 正确 |

### 🔍 技术细节

```bash
# 旧配置（错误）
arm-remarkable-linux-gnueabihf-gcc
-march=armv7-a -mfpu=neon -mfloat-abi=hard

# 新配置（正确）
aarch64-linux-gnu-gcc
-march=armv8-a -mtune=cortex-a53
```

### 📚 相关文档

- 架构说明：见 `README.md` 第59行
- 快速开始：`src/QUICKSTART.md`
- 实测方案：`REVISED_QT_SOLUTION.md`
- 部署指南：`QT5_DEPLOYMENT_GUIDE.md`

---

## 2025-01-XX - 初始版本 v1.0

### ✅ 完成的工作

- 创建基于Qt WebEngine的最小测试应用
- 开发自动化构建、部署、测试脚本
- 编写完整的验证文档体系
- 准备交叉编译工具链配置

### 📦 项目结构

- 测试应用代码（main.cpp, CMakeLists.txt）
- 自动化脚本（build.sh, deploy.sh, test.sh）
- 文档（README.md, QUICKSTART.md等）

### 🐛 已知问题

- ❌ 架构配置错误（ARMv7 vs ARMv8）← 已在v1.1修复

---

## 版本说明

- **v1.1** (当前): 架构修正 + Docker方案
- **v1.0**: 初始版本（有架构错误）
