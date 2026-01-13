# E-Ink 显示集成推进方案

**创建日期**: 2025-11-17
**最后更新**: 2025-11-17 (初始版本)
**设备**: reMarkable Paper Pro (i.MX93)

---

## 📊 当前状况

### ✅ 已完成
1. **Qt6 + WebEngine 完整编译** (Phase B-2)
   - 6,102 个构建目标 100% 完成
   - libQt6WebEngineCore.so (253 MB) ✅
   - 所有库和资源已部署到设备

2. **应用功能验证** (Phase B-5)
   - VNC 模式下完全正常运行
   - 微信读书网页加载成功
   - 多进程架构正常

3. **E-Ink 刷新代码已编写**
   - eink_refresh.h/cpp (基于 Oxide EPFramebuffer)
   - 集成到 PenFriendlyWebView
   - ⚠️ 但基于 /dev/fb0，不适用于 Paper Pro

### ❌ 核心问题

**reMarkable Paper Pro 架构变更**:

| 特性 | reMarkable 2 | reMarkable Paper Pro |
|------|--------------|----------------------|
| 处理器 | i.MX7 | **i.MX93** |
| Framebuffer | `/dev/fb0` ✅ | **不存在** ❌ |
| 显示接口 | mxcfb (传统 FB) | **DRM/KMS only** |
| 设备文件 | /dev/fb0 | /dev/dri/card0 |
| Qt 平台 | linuxfb 可用 | **需要 eglfs-kms** |

**当前 Qt6 配置不匹配**:
```cmake
# 当前配置
-DFEATURE_opengl=OFF      # ❌ Paper Pro 需要
-DFEATURE_eglfs=OFF       # ❌ Paper Pro 需要
-DFEATURE_linuxfb=ON      # ⚠️ 无法在 Paper Pro 使用

# 需要的配置
-DFEATURE_opengl=ON       # ✅ 启用 OpenGL ES
-DFEATURE_eglfs=ON        # ✅ 启用 eglfs
-DFEATURE_eglfs_kms=ON    # ✅ 启用 KMS 后端
```

---

## 🎯 推进方案

基于用户建议的"阶段 0-4"框架，调整为适应 Paper Pro 的新架构。

### 阶段 0: 保持"安全绳" ✅ 已完成

**目标**: 随时能确认"应用其实是正常跑着的"

**已验证的运行方式**:
1. ✅ **VNC 模式** - 可远程查看完整功能
   ```bash
   export QT_QPA_PLATFORM=vnc
   ./apps/weread-browser/bin/weread-browser
   # VNC 端口: 5900
   ```

2. ✅ **Offscreen 模式** - 验证逻辑正常
   ```bash
   export QT_QPA_PLATFORM=offscreen
   ./apps/weread-browser/bin/weread-browser
   ```

**现状**: ✅ 完成 - VNC 作为功能验证基线

---

### 阶段 1: 搞清 Paper Pro 如何刷新 E-Ink ⏳ 进行中

**目标**: 找到 Paper Pro 的 E-Ink 刷新机制

#### 1.1 观察 xochitl 的刷新行为 ⏳ 待执行

**方式 A: strace 追踪 ioctl** (首选)

```bash
# 在设备上执行
# 1. 找到 xochitl 进程
ps | grep xochitl

# 2. 追踪 ioctl 调用
strace -f -e trace=ioctl -p <xochitl_pid> 2>&1 | tee /tmp/xochitl_ioctl.log

# 3. 在界面上做一些操作（翻页、写字）
# 观察 ioctl 输出，寻找：
# - 操作的设备: /dev/dri/card0 还是其他
# - 重复出现的 ioctl 号
# - 刷新相关的数据结构
```

**预期发现**:
- DRM ioctl 号 (DRM_IOCTL_MODE_* 系列)
- 可能的自定义 ioctl (MXCFB_* 或新的接口)
- 刷新参数结构

**方式 B: 查找头文件**

```bash
# 在设备上搜索
find /usr/include -name "*.h" | xargs grep -l "eink\|epdc\|mxcfb\|drm" 2>/dev/null

# 特别关注:
# - /usr/include/drm/*.h
# - /usr/include/linux/mxcfb.h (如果存在)
# - 设备特定的头文件
```

**方式 C: 检查 xochitl 二进制**

```bash
# 查看 xochitl 依赖的库
readelf -d /usr/bin/xochitl | grep NEEDED

# 查看符号表，寻找刷新相关函数
nm -D /usr/lib/libqtsgepaper.so.1 2>/dev/null | grep -i "refresh\|update\|eink"

# 或者
strings /usr/lib/libqtsgepaper.so.1 | grep -i "ioctl\|drm\|refresh"
```

**执行状态**: ⬜ 未开始

**记录位置**: `docs-archive/EINK_IOCTL_MAP.md`

---

#### 1.2 研究 DRM/KMS E-Ink 接口 ⏳ 待执行

**目标**: 了解 i.MX93 DRM 驱动的 E-Ink 支持

**步骤**:

1. **检查 DRM 属性**
   ```bash
   # 安装工具 (如果有)
   # modetest -M imx-drm -p

   # 或手动查看 sysfs
   ls -la /sys/class/drm/
   cat /sys/class/drm/card0-*/status
   cat /sys/class/drm/card0-*/modes
   ```

2. **查看内核日志**
   ```bash
   dmesg | grep -i "eink\|epdc\|mxcfb\|lcdif"
   ```

3. **搜索类似项目**
   - 搜索 "i.MX93 E-Ink DRM" 相关资源
   - 查看 NXP 官方文档
   - 搜索其他 reMarkable Paper Pro 项目

**执行状态**: ⬜ 未开始

---

### 阶段 2: 编写独立的刷新工具 ⏳ 待执行

**目标**: 做一个"只负责刷新"的小程序

#### 2.1 基于阶段 1 的发现编写工具

**形态**: `rmm_eink_refresh_drm` - 最小的 DRM 刷新工具

```cpp
// eink_refresh_tool.cpp
#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <iostream>

int main(int argc, char **argv) {
    // 1. 打开 DRM 设备
    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) {
        perror("open /dev/dri/card0");
        return 1;
    }

    // 2. 获取 DRM 资源
    drmModeRes *resources = drmModeGetResources(fd);
    if (!resources) {
        perror("drmModeGetResources");
        close(fd);
        return 1;
    }

    std::cout << "Found " << resources->count_connectors << " connectors" << std::endl;
    std::cout << "Found " << resources->count_crtcs << " CRTCs" << std::endl;

    // 3. 根据阶段 1 的发现，调用刷新 ioctl
    // (这里需要填充具体的刷新代码)

    drmModeFreeResources(resources);
    close(fd);
    return 0;
}
```

**编译**:
```bash
# 在 Docker 容器中
cd /workspace
g++ -o rmm_eink_refresh_drm eink_refresh_tool.cpp -ldrm
```

#### 2.2 测试刷新工具

**测试步骤**:
1. 在 VNC 模式下启动应用，让它画一帧
2. 手动运行 `rmm_eink_refresh_drm`
3. 观察物理屏幕是否刷新

**成功标志**: 物理屏幕有任何变化（闪烁、刷新、显示内容）

**执行状态**: ⬜ 未开始

---

### 阶段 3: 集成到 Qt6 应用 ⏳ 待执行

**前提**: 阶段 2 的刷新工具能成功触发屏幕刷新

#### 3.1 可能需要：重新编译 Qt6 启用 eglfs

**原因**: Paper Pro 没有 /dev/fb0，Qt linuxfb 无法工作

**新的编译配置**:
```bash
cd /workspace/qt6-src/build-qt6
cmake -G Ninja ../qt6-src \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/opt/weread/qt6-eglfs \
  -DQT_BUILD_EXAMPLES=OFF \
  -DQT_BUILD_TESTS=OFF \
  -DFEATURE_webengine=ON \
  -DFEATURE_webengine_system_ffmpeg=OFF \
  -DFEATURE_webengine_proprietary_codecs=OFF \
  -DFEATURE_webengine_printing_and_pdf=OFF \
  -DFEATURE_webengine_extensions=OFF \
  -DFEATURE_webengine_spellchecker=OFF \
  -DFEATURE_opengl=ON \          # ✅ 启用 OpenGL ES
  -DFEATURE_eglfs=ON \            # ✅ 启用 eglfs
  -DFEATURE_eglfs_kms=ON \        # ✅ 启用 KMS 后端
  -DFEATURE_eglfs_gbm=ON \        # ✅ 启用 GBM
  -DFEATURE_linuxfb=ON            # 保留向后兼容

ninja -j$(nproc)
```

**时间估计**: 6-8 小时

**决策点**:
- 如果阶段 2 发现仍可以用某种方式访问 framebuffer → 不需要重新编译
- 如果必须使用 DRM/KMS 接口 → 需要重新编译

**执行状态**: ⬜ 未开始

#### 3.2 编写 E-Ink 刷新适配层

**基于阶段 2 的成功代码**:

```cpp
// eink_refresh_drm.h
class EInkRefresherDRM {
public:
    enum WaveformMode {
        INIT = 0,
        DU = 1,
        GC16 = 2,
        GL16 = 3,
        A2 = 6
    };

    static bool init();
    static void refreshFull(WaveformMode waveform = GC16);
    static void refreshPartial(const QRect& rect, WaveformMode waveform = DU);
    static void cleanup();

private:
    static int s_drmFd;
    static drmModeRes *s_resources;
    // ... DRM 相关状态
};
```

#### 3.3 集成到应用

**修改 main.cpp**:
```cpp
#include "eink_refresh_drm.h"

class PenFriendlyWebView : public QWebEngineView {
protected:
    void paintEvent(QPaintEvent *event) override {
        QWebEngineView::paintEvent(event);
        if (EInkRefresherDRM::isReady()) {
            scheduleRefresh();
        }
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // 初始化 DRM E-Ink 刷新
    if (EInkRefresherDRM::init()) {
        qDebug() << "E-Ink DRM refresh enabled";
    }

    // ...
}
```

**执行状态**: ⬜ 未开始

---

### 阶段 4: 优化体验 ⏳ 待执行

**前提**: 阶段 3 成功，物理屏幕能显示内容

**优化方向**:
1. **刷新策略**
   - 静态内容 → 部分刷新 (Partial)
   - 翻页 → 全屏刷新 (Full)
   - 滚动 → A2 快速刷新

2. **波形选择**
   - 文本 → GL16 (文本优化)
   - 图片 → GC16 (高质量)
   - 动画 → A2 (极速)

3. **刷新节流**
   - 当前: 500ms 间隔
   - 优化: 根据内容类型动态调整

4. **去残影**
   - 定期全屏 INIT 刷新
   - 检测停留时间，触发深度清理

**执行状态**: ⬜ 未开始

---

## 📋 执行时间表

| 阶段 | 预估时间 | 状态 | 开始时间 | 完成时间 |
|------|----------|------|----------|----------|
| 阶段 0 | - | ✅ 完成 | 2025-11-17 | 2025-11-17 |
| 阶段 1.1 | 1-2 小时 | ⏳ 待执行 | - | - |
| 阶段 1.2 | 1-2 小时 | ⏳ 待执行 | - | - |
| 阶段 2.1 | 1 小时 | ⏳ 待执行 | - | - |
| 阶段 2.2 | 30 分钟 | ⏳ 待执行 | - | - |
| 阶段 3.1 | 6-8 小时 | 🤔 可选 | - | - |
| 阶段 3.2 | 1 小时 | ⏳ 待执行 | - | - |
| 阶段 3.3 | 1 小时 | ⏳ 待执行 | - | - |
| 阶段 4 | 2-3 小时 | ⏳ 待执行 | - | - |

**总计**: 13-19 小时 (如果需要重新编译 Qt6)
**总计**: 7-11 小时 (如果不需要重新编译)

---

## 🔍 关键不确定性

1. **Paper Pro E-Ink 接口**:
   - ❓ 是否仍使用 mxcfb ioctl？
   - ❓ 是否有新的 DRM property 用于刷新？
   - ❓ 需要什么参数结构？

2. **Qt 平台需求**:
   - ❓ 是否必须重新编译 Qt6？
   - ❓ eglfs-kms 能否直接工作？
   - ❓ 是否需要自定义 QPA 插件？

3. **性能问题**:
   - ❓ DRM 刷新延迟如何？
   - ❓ 是否需要特殊优化？

**解决方式**: 通过阶段 1 和阶段 2 的实验来确定

---

## 📝 进度记录

### 2025-11-17 23:50 - 计划创建
- ✅ 创建推进方案文档
- ✅ 明确当前状况和问题
- ✅ 制定阶段性计划
- ⏭️ 下一步: 执行阶段 1.1 - strace 追踪 xochitl

---

## 🎯 立即行动

**现在开始执行阶段 1.1**:

```bash
# SSH 到设备
ssh root@10.11.99.1

# 找到 xochitl 进程 (假设它在运行)
ps | grep xochitl

# 追踪 ioctl
strace -f -e trace=ioctl -p <PID> 2>&1 | tee /tmp/xochitl_ioctl.log

# 在界面上操作，观察输出
# Ctrl+C 停止
# 将日志传回本地分析
```

**预期成果**: 找到 Paper Pro 的 E-Ink 刷新 ioctl 调用
