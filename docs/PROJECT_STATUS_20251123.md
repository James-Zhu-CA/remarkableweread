# WeRead reMarkable 项目状态报告

**生成时间**: 2025-11-23 20:30 UTC
**设备**: reMarkable Paper Pro Move (chiappa)
**分辨率**: 1696×954 (横屏), 954×1696 (竖屏)

---

## 📊 项目概览

### 当前架构：**双进程桥接方案（Two-Process Bridge MVP）**

```
┌─────────────────────────────────────────────────────────────┐
│  前端进程 (WereadEinkBrowser)                                │
│  - Qt6 WebEngine (offscreen)                                 │
│  - 渲染微信读书网页 (954×1696, zoom 2.0x)                   │
│  - 捕获帧 → 写入共享内存 /dev/shm/weread_frame              │
│  - UDP 接收触摸事件 (127.0.0.1:45454)                       │
└──────────────────┬──────────────────────────────────────────┘
                   │ 共享内存 (12.3MB ARGB32 双缓冲)
                   ↓
┌─────────────────────────────────────────────────────────────┐
│  后端进程 (epaper_shm_viewer)                                │
│  - 系统 Qt + epaper 插件                                     │
│  - 读取共享内存 → 显示到 E-Ink 屏幕                         │
│  - 触摸事件 → UDP 发送到前端 (127.0.0.1:45454)              │
└─────────────────────────────────────────────────────────────┘
```

---

## ✅ 已完成功能

### 1. **前端进程 (WereadEinkBrowser)** ✅

**代码位置**: [src/app/main.cpp](src/app/main.cpp)

**核心功能**:
- ✅ Qt6 WebEngine offscreen 渲染
- ✅ 自动缩放 2.0x（提高可读性）
- ✅ 共享内存帧缓冲写入器 ([shm_writer.cpp](src/app/shm_writer.cpp))
- ✅ UDP 触摸事件接收器 (端口 45454)
- ✅ 触摸事件注入到 WebEngine
- ✅ JavaScript 点击和滚动支持

**数据协议**: [shm_proto.h](bridge-mvp/shm_proto.h)
```cpp
struct ShmHeader {
    uint32_t magic = 0x5752464d;  // 'WRFM'
    uint32_t version = 1;
    uint32_t width = 954;          // 横屏
    uint32_t height = 1696;
    uint32_t stride = 954 * 4;     // ARGB32
    uint32_t format = 1;           // ARGB32
    uint32_t gen_counter;          // 帧计数器
    uint32_t active_buffer;        // 0 或 1
};
// 后跟两个帧缓冲：stride × height × 2
```

**运行命令**:
```bash
export QT_QPA_PLATFORM=offscreen
export QTWEBENGINE_DISABLE_SANDBOX=1
/home/root/weread/WereadEinkBrowser
```

**状态**: ✅ 已部署，正在运行 (PID 5507)

### 2. **后端进程 (epaper_shm_viewer)** ✅

**代码位置**: [bridge-mvp/viewer/main.cpp](bridge-mvp/viewer/main.cpp)

**核心功能**:
- ✅ mmap 读取 `/dev/shm/weread_frame`
- ✅ 每 200ms 轮询 `gen_counter` 变化
- ✅ QML Image Provider 更新显示
- ✅ 触摸/鼠标事件捕获 → UDP 发送
- ✅ QML 显示界面 ([main.qml](bridge-mvp/viewer/main.qml))

**运行要求**:
- 使用系统 Qt（带 epaper 插件）
- **关键**: 需要 epaper 插件处理 E-Ink 刷新

**状态**: ✅ 已部署，正在运行 (PID 5543)

### 3. **共享内存文件** ✅

**验证**:
```bash
$ ls -lh /dev/shm/weread_frame
-rw-r--r-- 1 root root 12.3M Nov 24 02:15 weread_frame
```

**大小计算**:
- Header: `sizeof(ShmHeader)` = 64 bytes
- Buffer0: 954 × 1696 × 4 = 6,470,016 bytes
- Buffer1: 954 × 1696 × 4 = 6,470,016 bytes
- Total: 64 + 12,940,032 = **12,940,096 bytes ≈ 12.3 MB** ✅

### 4. **官方 EPaper 库分析** ✅

**提取的官方库**:
- [libqsgepaper.so](official-epaper-libs/libqsgepaper.so) (521 KB) - Qt Scene Graph EPaper 插件
- [libepaper.so](official-epaper-libs/libepaper.so) (262 KB) - Qt Platform EPaper 插件

**关键发现**: 📄 [OFFICIAL_LIBS_ANALYSIS.md](official-epaper-libs/OFFICIAL_LIBS_ANALYSIS.md)
1. **官方也使用 DRM**: libqsgepaper.so 依赖 libdrm.so.2
2. **波形文件**: 4 个刷新模式（fast/pen/std/best）
3. **显示参数源**: Device Tree (`/sys/firmware/devicetree/base/display-info/`)
4. **锁文件机制**: `/tmp/epframebuffer.lock`, `/tmp/epd.lock`

---

## ⚠️ 当前问题

### 🔴 **主要问题：E-Ink 刷新缺失**

#### 问题描述
- **现象**: 应用运行正常，有网络，但**白屏无显示**
- **原因**: 后端进程 (epaper_shm_viewer) **没有触发 E-Ink 刷新**

#### 根本原因分析

**问题 1: epaper 插件可能未加载**

后端进程需要使用**系统 Qt + epaper 插件**，但可能：
- 未正确加载 epaper 平台插件
- 或 epaper 插件本身有问题

**验证方法**:
```bash
# 检查 epaper_shm_viewer 是否使用了 epaper 插件
ldd /home/root/weread/apps/shm-viewer/epaper_shm_viewer | grep epaper

# 检查环境变量
ps e -p 5543 | grep QT_QPA
```

**问题 2: 缺少明确的刷新触发**

查看 [viewer/main.cpp:27-46](bridge-mvp/viewer/main.cpp#L27-L46)：
```cpp
class ShmImageProvider : public QQuickImageProvider {
    QImage requestImage(const QString &, QSize *size, const QSize &) override {
        if (!m_hdr || !m_buf0) return {};
        const uint8_t *src = (m_hdr->active_buffer == 0) ? m_buf0 : m_buf1;
        QImage img(src, ..., m_format);
        return img.copy(); // ⚠️ 只是拷贝 QImage，没有刷新调用
    }
};
```

**缺失**:
- ❌ 没有调用 `window()->update()` 触发重绘
- ❌ 没有明确的 E-Ink 刷新 API 调用

**QML 更新机制** ([viewer/main.qml:26](bridge-mvp/viewer/main.qml#L26)):
```qml
Image {
    source: "image://shmframe/frame?gen=" + shmWatcher.gen
    // gen 变化时会重新请求 Image，但不一定触发屏幕刷新
}
```

#### 对比：官方 epaper 插件如何工作？

根据 [OFFICIAL_LIBS_ANALYSIS.md](official-epaper-libs/OFFICIAL_LIBS_ANALYSIS.md)：

**libepaper.so 的关键类**:
```cpp
class EpaperBackingStore : public QPlatformBackingStore {
    QPaintDevice* paintDevice();
    void flush(QWindow*, const QRegion&, const QPoint&); // ⭐ 关键：flush 触发刷新
    void resize(const QSize&, const QRegion&);
};
```

**正常流程**:
1. QML/Qt 应用渲染到 `QPaintDevice`
2. 调用 `EpaperBackingStore::flush()`
3. epaper 插件内部调用 DRM ioctl 刷新 E-Ink

**我们的问题**:
- epaper_shm_viewer 可能没有使用正确的平台插件
- 或者 QML Image 更新不会触发 `flush()`

---

## 🔧 解决方案

### **方案 A：确保 epaper 插件正确加载（推荐）**

#### A1. 修改 viewer 启动方式

**创建启动脚本**: `run-epaper-viewer.sh`
```bash
#!/bin/sh
export QT_QPA_PLATFORM=epaper           # ⭐ 明确使用 epaper 平台
export QT_QPA_EGLFS_INTEGRATION=none    # 禁用 EGLFS
export QT_QUICK_BACKEND=software        # 使用软件渲染

cd /home/root/weread/apps/shm-viewer
./epaper_shm_viewer
```

#### A2. 检查插件路径

```bash
# 查看系统 Qt 插件目录
ls /usr/lib/plugins/platforms/

# 应该看到：
# libepaper.so
# liblinuxfb.so
# ...
```

如果 epaper_shm_viewer 是用自定义 Qt6 编译的，需要：
```bash
export QT_PLUGIN_PATH=/usr/lib/plugins  # 使用系统插件
```

### **方案 B：添加显式刷新调用**

#### B1. 修改 ShmWatcher 触发窗口更新

**修改**: [bridge-mvp/viewer/main.cpp:96-103](bridge-mvp/viewer/main.cpp#L96-L103)

```cpp
class ShmWatcher : public QObject {
    Q_OBJECT
    Q_PROPERTY(uint gen READ gen NOTIFY genChanged)

public:
    // NEW: 添加 window 引用
    void setWindow(QQuickWindow *win) { m_window = win; }

private slots:
    void poll() {
        if (!m_hdr) return;
        if (m_hdr->gen_counter != m_gen) {
            m_gen = m_hdr->gen_counter;
            emit genChanged();

            // NEW: 强制窗口刷新
            if (m_window) {
                m_window->update();  // 触发重绘
            }
        }
    }

private:
    QQuickWindow *m_window = nullptr;  // NEW
    // ... 其他成员
};

int main(int argc, char *argv[]) {
    // ...
    engine.load(url);

    // NEW: 获取窗口并设置到 watcher
    auto rootObjects = engine.rootObjects();
    if (!rootObjects.isEmpty()) {
        QQuickWindow *window = qobject_cast<QQuickWindow*>(rootObjects.first());
        if (window) {
            watcher.setWindow(window);
        }
    }

    return app.exec();
}
```

#### B2. 使用 QQuickRenderControl 强制渲染

如果上述方法不够，考虑使用低级 API：

```cpp
#include <QQuickRenderControl>

// 在 poll() 中
if (m_window && m_renderControl) {
    m_renderControl->polishItems();
    m_renderControl->sync();
    m_renderControl->render();
}
```

### **方案 C：直接调用 DRM 刷新（绕过 Qt）**

#### C1. 在 viewer 中添加 DRM 刷新器

**新文件**: `bridge-mvp/viewer/drm_refresher.h`

```cpp
#pragma once
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <QRect>

// 从官方库分析中得知的 DRM 函数
extern "C" {
    int drmModeAddFB(int fd, uint32_t width, uint32_t height,
                     uint8_t depth, uint8_t bpp, uint32_t pitch,
                     uint32_t bo_handle, uint32_t *buf_id);
    int drmModeSetCrtc(int fd, uint32_t crtcId, uint32_t bufferId,
                       uint32_t x, uint32_t y, uint32_t *connectors,
                       int count, void *mode);
    int drmModeRmFB(int fd, uint32_t bufferId);
}

class DrmRefresher {
public:
    DrmRefresher() {
        m_fd = open("/dev/dri/card0", O_RDWR);
        if (m_fd < 0) {
            qWarning() << "Failed to open DRM device";
        }
    }

    ~DrmRefresher() {
        if (m_fd >= 0) close(m_fd);
    }

    void refresh(const QRect &rect = QRect()) {
        if (m_fd < 0) return;

        // 基于官方库的 DRM 调用流程
        // TODO: 从 official-epaper-libs 分析中补充具体参数

        // 1. drmModeAddFB - 添加帧缓冲
        // 2. drmModeSetCrtc - 设置显示控制器
        // 3. (可选) 特定的 E-Ink 刷新 ioctl
        // 4. drmModeRmFB - 移除旧帧缓冲
    }

private:
    int m_fd = -1;
};
```

#### C2. 集成到 ShmWatcher

```cpp
class ShmWatcher : public QObject {
    // ...
private:
    DrmRefresher m_refresher;  // NEW

    void poll() {
        if (!m_hdr) return;
        if (m_hdr->gen_counter != m_gen) {
            m_gen = m_hdr->gen_counter;
            emit genChanged();

            // NEW: 直接刷新 E-Ink
            m_refresher.refresh();
        }
    }
};
```

---

## 📋 下一步行动计划

### **Phase 1: 诊断当前状态（1小时）**

#### 任务 1.1: 检查 epaper 插件加载
```bash
# SSH 到设备
ssh root@10.11.99.1

# 查看 epaper_shm_viewer 的库依赖
ldd /home/root/weread/apps/shm-viewer/epaper_shm_viewer

# 查看运行环境
cat /proc/5543/environ | tr '\0' '\n' | grep QT
```

#### 任务 1.2: 测试官方 epaper 插件
```bash
# 创建最简 QML 测试
cat > /tmp/test.qml << 'EOF'
import QtQuick 2.15
import QtQuick.Window 2.15

Window {
    visible: true
    width: 954
    height: 1696

    Rectangle {
        anchors.fill: parent
        color: "white"

        Rectangle {
            anchors.centerIn: parent
            width: 400
            height: 200
            color: "black"

            Text {
                anchors.centerIn: parent
                text: "EPaper Plugin Test"
                color: "white"
                font.pixelSize: 32
            }
        }
    }
}
EOF

# 用系统 Qt + epaper 插件运行
QT_QPA_PLATFORM=epaper qml /tmp/test.qml
```

**预期结果**:
- ✅ 如果屏幕显示黑色矩形和文字 → epaper 插件工作正常
- ❌ 如果白屏 → epaper 插件有问题，需要其他方案

### **Phase 2: 实施修复（2-4小时）**

根据 Phase 1 的结果选择方案：

#### 情况 A: epaper 插件工作正常
→ 实施**方案 A** 或**方案 B**（添加显式刷新调用）

**步骤**:
1. 修改 `bridge-mvp/viewer/main.cpp`
2. 重新编译 epaper_shm_viewer
3. 部署到设备测试

#### 情况 B: epaper 插件不工作
→ 实施**方案 C**（直接 DRM 刷新）

**步骤**:
1. 研究 [official-epaper-libs](official-epaper-libs/) 中的 DRM 调用
2. 实现 DrmRefresher 类
3. 集成到 epaper_shm_viewer
4. 测试刷新

### **Phase 3: 优化和完善（可选）**

一旦显示工作：
- [ ] 优化刷新频率（避免过度刷新）
- [ ] 实现局部刷新（只刷新变化区域）
- [ ] 添加波形模式选择（fast/best/pen/std）
- [ ] 集成官方 waveform 文件（`/usr/share/remarkable/ct33_*.bin`）

---

## 📚 相关文档

### 项目文档
- [x] [BRIDGE_TWO_PROCESS_MVP.md](BRIDGE_TWO_PROCESS_MVP.md) - 双进程架构设计
- [x] [EINK_REFRESH_SOLUTION.md](EINK_REFRESH_SOLUTION.md) - E-Ink 刷新问题分析
- [x] [STAGE1_REVISED_STRATEGY.md](docs-archive/STAGE1_REVISED_STRATEGY.md) - qtfb-shim 策略
- [x] [OFFICIAL_LIBS_ANALYSIS.md](official-epaper-libs/OFFICIAL_LIBS_ANALYSIS.md) - 官方库分析

### 代码位置
- **前端**: [src/app/](src/app/)
  - [main.cpp](src/app/main.cpp) - 主程序
  - [shm_writer.h](src/app/shm_writer.h) - 共享内存写入器

- **后端**: [bridge-mvp/viewer/](bridge-mvp/viewer/)
  - [main.cpp](bridge-mvp/viewer/main.cpp) - QML 查看器
  - [main.qml](bridge-mvp/viewer/main.qml) - QML 界面

- **协议**: [bridge-mvp/shm_proto.h](bridge-mvp/shm_proto.h)

### 设备上的文件
```
/home/root/weread/
├── WereadEinkBrowser              # 前端进程（正在运行）
├── apps/
│   └── shm-viewer/
│       └── epaper_shm_viewer      # 后端进程（正在运行）
├── lib/                           # Qt6 库
├── plugins/                       # Qt6 插件
└── qt6/                          # Qt6 运行时

/dev/shm/
└── weread_frame (12.3MB)         # 共享内存帧缓冲 ✅

/usr/lib/plugins/
├── platforms/
│   └── libepaper.so              # 官方 epaper 平台插件
└── scenegraph/
    └── libqsgepaper.so           # 官方 Scene Graph 插件
```

---

## 🎯 成功标准

### Phase 1 成功标准
- [ ] 确认 epaper 插件加载状态
- [ ] 测试官方 epaper 插件功能
- [ ] 识别刷新缺失的具体原因

### Phase 2 成功标准
- [ ] epaper_shm_viewer 能显示共享内存内容
- [ ] 屏幕能正确刷新（不再白屏）
- [ ] 触摸输入能正常工作

### Phase 3 成功标准（可选）
- [ ] 刷新性能优化（局部刷新）
- [ ] 波形模式选择（提升显示质量）
- [ ] 稳定运行 1 小时以上

---

## 📊 技术债务

### 已知限制
1. **刷新频率**: 前端固定 1 秒捕获一次帧（较慢）
   - 建议：改为按需捕获或更高频率

2. **内存拷贝**: QImage copy() 每次都拷贝 6.4 MB
   - 建议：使用 zero-copy 或 wrapping QImage

3. **触摸延迟**: UDP 通信有额外延迟
   - 建议：直接在后端注入输入事件到内核

4. **无局部刷新**: 每次全屏刷新
   - 建议：实现脏区域跟踪

### 潜在风险
- ⚠️ 系统 Qt 版本兼容性（系统 Qt 可能是 Qt5）
- ⚠️ epaper 插件私有 API 变化
- ⚠️ DRM 驱动参数在未来固件更新中变化

---

## 🔍 调试技巧

### 查看日志
```bash
# 前端日志
tail -f /tmp/weread-offscreen.log

# 后端日志（如果有）
journalctl -f | grep epaper

# 系统 DRM 日志
dmesg | grep drm
```

### 监控共享内存
```bash
# 监控文件大小变化
watch -n 1 'ls -lh /dev/shm/weread_frame'

# 查看 header 内容（十六进制）
hexdump -C /dev/shm/weread_frame | head -4
```

### 性能分析
```bash
# CPU 使用率
top -p $(pgrep WereadEinkBrowser),$(pgrep epaper_shm_viewer)

# 内存使用
cat /proc/5507/status | grep -E "VmSize|VmRSS"
cat /proc/5543/status | grep -E "VmSize|VmRSS"
```

---

**报告结束**

*如需更新或补充，请修改此文档*
