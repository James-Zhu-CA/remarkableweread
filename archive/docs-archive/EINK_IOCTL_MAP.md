# reMarkable Paper Pro E-Ink 接口分析

**日期**: 2025-11-17
**设备**: reMarkable Paper Pro (i.MX93)

---

## 🔍 关键发现

### 1. E-Paper 平台插件

**设备上的关键库**:
- `/usr/lib/plugins/platforms/libepaper.so` - Qt Platform Plugin (QPA)
- `/usr/lib/plugins/scenegraph/libqsgepaper.so` - Qt SceneGraph E-Paper 渲染器
- `/usr/lib/libdrm.so.2.4.0` - DRM 库

### 2. xochitl 使用的设备

通过检查 `/proc/<xochitl_pid>/fd/` 发现:
```bash
lrwx------    1 root     root            64 Nov 18 01:01 17 -> /dev/dri/card0
```

**确认**: reMarkable Paper Pro 使用 **DRM/KMS**，不使用传统的 `/dev/fb0`

### 3. E-Paper 核心类 (从 libqsgepaper.so)

#### EPFramebuffer 类

**关键方法**:
```cpp
// 主要刷新接口
void EPFramebuffer::swapBuffers(
    const QRect& region,
    const EPContentMap& contentMap,
    const EPScreenModeMap& screenModeMap,
    QFlags<EPFramebuffer::UpdateFlag> flags
);

// 简化接口
void EPFramebuffer::swapBuffers(
    QRect rect,
    EPContentType contentType,
    EPScreenMode screenMode,
    QFlags<EPFramebuffer::UpdateFlag> flags
);

// 帧缓冲更新信号
void EPFramebuffer::framebufferUpdated(const QRect& rect);
```

#### EPFramebuffer 实现类

1. **EPFramebufferAcep2** (Advanced Color E-Paper 2)
   - 可能用于 Paper Pro (新设备)
   - 方法:
     - `sendTModeUpdate()` - 发送 T-Mode 更新
     - `scheduleTModeUpdate()` - 调度 T-Mode 更新

2. **EPFramebufferSwtcon**
   - 可能用于 reMarkable 2
   - 方法:
     - `update(QRect rect, int mode, PixelMode pixelMode, int waveform)`

### 4. E-Paper 相关枚举和类型

**推测的类型** (基于符号):
```cpp
enum class EPContentType {
    // 未知具体值
};

enum class EPScreenMode {
    // 未知具体值
};

enum class PixelMode {
    // 未知具体值
};

class EPContentMap {
    // 区域到内容类型的映射
};

class EPScreenModeMap {
    // 区域到屏幕模式的映射
};

class EPFramebuffer::UpdateFlag {
    // 更新标志 (QFlags)
};
```

### 5. Qt SceneGraph 集成

**EPRenderLoop** - E-Paper 专用渲染循环:
```cpp
void EPRenderLoop::update(QQuickWindow *window);
void EPRenderLoop::maybeUpdate(QQuickWindow *window);
void EPRenderLoop::handleUpdateRequest(QQuickWindow *window);
```

**EPRenderBlocker** - 更新阻塞器:
```cpp
bool EPRenderBlocker::isBlockingUpdates() const;
void EPRenderBlocker::isBlockingUpdatesChanged();
```

**EPImageNode** - E-Paper 图像节点:
```cpp
void EPImageNode::update();
void EPImageNode::updateCached();
```

---

## 📊 架构分析

### Paper Pro 显示栈

```
应用层: QQuickApplication / QGuiApplication
   ↓
Qt QPA 层: libepaper.so (EpaperIntegration)
   ↓
Qt SceneGraph: libqsgepaper.so (EPFramebuffer, EPRenderLoop)
   ↓
DRM/KMS: libdrm.so → /dev/dri/card0
   ↓
内核驱动: imx-drm + imx-lcdifv3-crtc
   ↓
硬件: i.MX93 Display Controller → E-Ink 面板
```

### 对比 reMarkable 2 vs Paper Pro

| 特性 | reMarkable 2 | Paper Pro |
|------|--------------|-----------|
| Framebuffer | `/dev/fb0` | **不存在** |
| DRM 设备 | 可能有 | `/dev/dri/card0` |
| Qt 平台插件 | libepaper.so | libepaper.so (同名) |
| Framebuffer 类 | EPFramebufferSwtcon? | **EPFramebufferAcep2** |
| 显示技术 | Monochrome E-Ink | **Advanced Color E-Paper 2** |

---

## 💡 集成方案

### 方案 A: 直接使用 libepaper.so ⭐ 推荐

**思路**: 在我们的 Qt6 应用中加载系统的 E-Paper 平台插件

**实现**:
```cpp
// 在启动时指定平台插件
export QT_QPA_PLATFORM=epaper
export QT_QPA_PLATFORM_PLUGIN_PATH=/usr/lib/plugins/platforms

./apps/weread-browser/bin/weread-browser
```

**优点**:
- ✅ 使用官方实现，稳定可靠
- ✅ 自动处理所有 E-Ink 刷新逻辑
- ✅ 支持颜色 E-Paper (ACEP2)
- ✅ 无需编写刷新代码

**挑战**:
- ⚠️ 需要确保我们的 Qt6 与系统 Qt6 ABI 兼容
- ⚠️ 可能需要额外的环境配置

**可行性**: 🔶 需要测试

### 方案 B: 链接 libqsgepaper.so

**思路**: 在我们的应用中链接 E-Paper SceneGraph 库

**实现**:
```cmake
# CMakeLists.txt
target_link_libraries(weread-browser
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::WebEngineWidgets
    /usr/lib/libqsgepaper.so  # ← 链接系统库
)
```

```cpp
// main.cpp
#include <EPFramebuffer>  // 需要找到头文件

int main() {
    // 使用 EPFramebuffer API
}
```

**优点**:
- ✅ 可以精细控制刷新行为
- ✅ 不依赖平台插件

**挑战**:
- ❌ 缺少头文件 (`.h`)
- ❌ 需要逆向推导 API
- ❌ ABI 兼容性问题

**可行性**: 🔴 困难

### 方案 C: 研究 DRM ioctl，自己实现

**思路**: 理解 libqsgepaper.so 的底层 ioctl 调用，自己编写刷新代码

**步骤**:
1. 使用 strace 追踪 xochitl 的 ioctl
2. 找到 E-Ink 刷新的 DRM ioctl 号和参数结构
3. 编写 C++ 封装

**优点**:
- ✅ 完全控制
- ✅ 不依赖系统库

**挑战**:
- ⚠️ 需要逆向工程
- ⚠️ 可能需要大量测试

**可行性**: 🟡 中等

### 方案 D: 重新编译 Qt6 启用 eglfs + 加载 epaper 插件

**思路**: 编译支持 eglfs 的 Qt6，然后使用系统 epaper 插件

**步骤**:
1. 重新编译 Qt6:
   ```bash
   -DFEATURE_eglfs=ON
   -DFEATURE_eglfs_kms=ON
   -DFEATURE_opengl=ON
   ```
2. 运行时指定:
   ```bash
   export QT_QPA_PLATFORM=epaper
   ```

**优点**:
- ✅ 官方路径
- ✅ 稳定可靠

**挑战**:
- ⏱️ 需要 6-8 小时重新编译
- ⚠️ ABI 兼容性仍需验证

**可行性**: 🟢 高

---

## 🎯 推荐执行顺序

### 第一步: 测试方案 A (立即可测试)

```bash
# SSH 到设备
ssh root@10.11.99.1

# 停止 xochitl
systemctl stop xochitl

# 尝试使用 epaper 平台
cd /home/root/weread
export QT_QPA_PLATFORM=epaper
export QT_PLUGIN_PATH=/usr/lib/plugins
export LD_LIBRARY_PATH=/home/root/weread/qt6/lib:/usr/lib
./apps/weread-browser/bin/weread-browser 2>&1 | tee /tmp/epaper-test.log
```

**预期结果**:
- ✅ 最理想: 应用启动，E-Ink 屏幕显示正常
- 🟡 可能: 插件加载失败，ABI 不兼容
- ❌ 最坏: 应用崩溃

### 第二步: 如果方案 A 失败 → 方案 C (strace 研究)

**需要 strace**:
```bash
# 如果设备上没有 strace，从 Docker 容器复制
docker exec qt6-arm-builder bash -c 'which strace'
# 或者交叉编译一个静态链接的 strace
```

### 第三步: 如果方案 C 可行 → 实现自定义刷新

基于 strace 发现编写 `eink_refresh_drm.cpp`

### 第四步: 如果都不行 → 方案 D (重新编译 Qt6)

---

## 📝 下一步行动

**立即执行** (5-10 分钟):
1. ✅ 测试方案 A: 使用系统 epaper 平台插件
2. 📝 记录测试结果

**如果方案 A 失败**:
1. 获取 strace 工具
2. 追踪 xochitl 的 DRM ioctl 调用
3. 分析刷新机制

---

**更新时间**: 2025-11-17 23:55
**状态**: 阶段 1.1 完成 - 已找到 E-Paper 插件和核心类
