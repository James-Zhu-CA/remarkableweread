# E-Ink 显示集成 - 当前状态总结

**更新时间**: 2025-11-18 01:20
**设备**: reMarkable Paper Pro (i.MX93)

---

## 📊 当前状态

### ✅ 已完成的工作

1. **阶段 1.1 - E-Paper 库发现** ✅ 完成
   - 找到系统 E-Paper 插件: `/usr/lib/plugins/platforms/libepaper.so`
   - 找到 SceneGraph 库: `/usr/lib/plugins/scenegraph/libqsgepaper.so`
   - 分析了关键类: `EPFramebuffer`, `EPFramebufferAcep2`, `EPRenderLoop`
   - 确认设备使用 DRM/KMS (`/dev/dri/card0`)

2. **方案 A 测试 - 使用系统 epaper 插件** ❌ 失败
   - libepaper.so 能被加载
   - 但随后应用崩溃 (SIGBUS - signal 7)
   - **根本原因**: ABI 不兼容
     - 我们的 Qt6: 6.8.2
     - 系统 Qt6: 6.0.x
   - **结论**: 无法直接使用系统库

---

## 🔍 关键技术发现

### Paper Pro 显示架构

```
应用
  ↓
Qt Platform Plugin (libepaper.so)
  ↓ EPFramebufferAcep2
libdrm.so (DRM/KMS)
  ↓
/dev/dri/card0
  ↓
内核: imx-drm + imx-lcdifv3-crtc
  ↓
i.MX93 Display Controller → E-Ink
```

### E-Paper 关键类 (从 libqsgepaper.so)

```cpp
// 主要刷新接口
class EPFramebuffer {
    void swapBuffers(QRect, EPContentType, EPScreenMode, UpdateFlags);
    void framebufferUpdated(const QRect&);
};

// Paper Pro 实现
class EPFramebufferAcep2 : public EPFramebuffer {
    void sendTModeUpdate();
    void scheduleTModeUpdate();
};

// reMarkable 2 实现
class EPFramebufferSwtcon : public EPFramebuffer {
    void update(QRect, int mode, PixelMode, int waveform);
};

// 渲染循环
class EPRenderLoop {
    void update(QQuickWindow*);
    void handleUpdateRequest(QQuickWindow*);
};
```

---

## 🎯 可行的方案 (基于测试结果)

### ❌ 方案 A: 直接使用 libepaper.so
- **状态**: 已测试，失败
- **原因**: ABI 不兼容 (Qt 6.0.x vs 6.8.2)
- **不再考虑**

### 🟡 方案 B: 重新编译 Qt6 启用 eglfs + 尝试兼容系统插件
**状态**: 可能可行，但不确定性高

**步骤**:
1. 重新编译 Qt6:
   ```cmake
   -DFEATURE_eglfs=ON
   -DFEATURE_eglfs_kms=ON
   -DFEATURE_opengl=ON
   ```
2. 尝试加载系统 libepaper.so
3. 如果仍然 ABI 不兼容 → 转方案 C

**优点**:
- 如果能兼容系统插件，问题立即解决
- eglfs 是 Paper Pro 正确的平台

**缺点**:
- 耗时 6-8 小时重新编译
- 仍可能 ABI 不兼容
- 风险高

**可行性**: 🟡 中等 (30-40%)

---

### 🟢 方案 C: 重新编译 Qt6 + 自研 DRM E-Ink 刷新 (推荐)
**状态**: 推荐路径

**原理**:
- 编译支持 eglfs 的 Qt6 (Paper Pro 需要)
- 自己编写 E-Ink 刷新代码 (不依赖 libepaper.so)
- 通过 DRM ioctl 直接控制刷新

**步骤**:

**1. 重新编译 Qt6 (6-8 小时)**
```bash
cd /workspace/qt6-src/build-qt6-eglfs
cmake -G Ninja ../qt6-src \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/opt/weread/qt6-eglfs \
  -DFEATURE_webengine=ON \
  -DFEATURE_opengl=ON \          # ✅ 启用
  -DFEATURE_eglfs=ON \            # ✅ 启用
  -DFEATURE_eglfs_kms=ON \        # ✅ 启用
  -DFEATURE_eglfs_gbm=ON \        # ✅ 启用
  -DFEATURE_linuxfb=ON            # 保留

ninja -j$(nproc)
```

**2. 研究 Paper Pro DRM E-Ink 刷新机制 (2-4 小时)**

选项 2a: 反向工程 libqsgepaper.so
```bash
# 使用 objdump, nm, strings 分析
objdump -d /tmp/libqsgepaper.so > qsgepaper_disasm.txt
# 查找 ioctl 调用和参数
```

选项 2b: strace 追踪 xochitl (推荐)
```bash
# 需要在设备上获取 strace 工具
# 追踪 xochitl 的 DRM ioctl
strace -f -e trace=ioctl -p <xochitl_pid> 2>&1 | grep "dri/card0"
```

选项 2c: 查找开源项目
- 搜索 "i.MX93 E-Ink DRM"
- 搜索 "reMarkable Paper Pro E-Ink"
- 查看是否有其他项目已经解决

**3. 编写 E-Ink 刷新适配层 (2-3 小时)**

基于发现编写:
```cpp
// eink_drm_refresh.h
class EInkDRMRefresher {
public:
    static bool init();
    static void refreshFull();
    static void refreshPartial(const QRect& rect);
private:
    static int s_drmFd;
    // DRM 相关状态
};

// eink_drm_refresh.cpp
bool EInkDRMRefresher::init() {
    s_drmFd = open("/dev/dri/card0", O_RDWR);
    // ... 初始化 DRM 资源
}

void EInkDRMRefresher::refreshFull() {
    // 根据阶段 2 的发现实现
    // 可能使用:
    // - DRM property (drm_mode_obj_set_property)
    // - 自定义 ioctl
    // - DRM plane commit
}
```

**4. 集成到应用 (1 小时)**
```cpp
// main.cpp
#include "eink_drm_refresh.h"

class PenFriendlyWebView : public QWebEngineView {
protected:
    void paintEvent(QPaintEvent *event) override {
        QWebEngineView::paintEvent(event);
        if (EInkDRMRefresher::isReady()) {
            scheduleRefresh();
        }
    }
};
```

**优点**:
- ✅ 完全控制刷新逻辑
- ✅ 不依赖系统库，无 ABI 问题
- ✅ 可以针对 Paper Pro 优化

**缺点**:
- ⏱️ 需要时间 (11-16 小时总计)
- 🔧 需要逆向工程或研究

**可行性**: 🟢 高 (70-80%)

---

### 🔵 方案 D: 使用 VNC 作为临时方案
**状态**: 已验证可行，作为开发期间的临时方案

**用途**:
- ✅ 开发和测试应用功能
- ✅ 验证 WebEngine 加载、网页渲染等
- ❌ 不是最终发布方案

---

## 📋 推荐执行路线

### 短期 (今天):
1. ✅ 总结当前发现 (已完成)
2. 📝 更新 PHASE_B_REVISED_PLAN.md
3. 💤 休息

### 中期 (明天 - 2天内):

**选项 1: 保守路径 (推荐给稳定性优先)**
1. 重新编译 Qt6 启用 eglfs (6-8 小时)
2. 研究 DRM E-Ink 刷新机制 (2-4 小时)
3. 编写自定义刷新代码 (2-3 小时)
4. 测试集成 (1-2 小时)

**选项 2: 激进路径 (推荐给时间紧迫)**
1. 先研究 DRM E-Ink 刷新机制 (2-4 小时)
2. 如果能快速找到 ioctl 接口:
   - 在当前 Qt6 (linuxfb) 上先测试刷新 (可能通过 VNC + DRM ioctl)
3. 确认可行后再重新编译 Qt6

### 长期 (1周内):
1. 完整测试和优化
2. 刷新策略调优
3. 性能优化
4. 文档编写

---

## 🔧 待研究的关键问题

1. **Paper Pro 的 DRM E-Ink 刷新方式**:
   - ❓ 使用哪个 ioctl? (DRM standard 还是自定义)
   - ❓ 参数结构是什么?
   - ❓ 是否有 DRM property 用于刷新控制?

2. **EPFramebufferAcep2 的实现细节**:
   - ❓ 什么是 T-Mode?
   - ❓ ACEP2 (Advanced Color E-Paper 2) 刷新机制?

3. **eglfs 平台需求**:
   - ❓ 是否需要特殊的 eglfs 配置文件?
   - ❓ Mesa/EGL 库依赖?

---

## 🎯 立即行动 (如果要继续工作)

**方案 C - 选项 2b: 获取 strace 追踪 xochitl** (30分钟 - 1小时)

```bash
# 步骤 1: 在 Docker 中找到或编译 strace
docker exec qt6-arm-builder bash -c 'which strace'

# 如果有,复制到设备
docker cp qt6-arm-builder:/usr/bin/strace .
scp strace root@10.11.99.1:/tmp/

# 步骤 2: 在设备上追踪 xochitl
ssh root@10.11.99.1
ps | grep xochitl  # 找到 PID
/tmp/strace -f -e trace=ioctl -p <PID> 2>&1 | tee /tmp/xochitl_ioctl.log

# 步骤 3: 在界面上操作 (翻页、画画)
# 步骤 4: Ctrl+C 停止,分析日志
```

**预期发现**: DRM ioctl 调用和参数，用于 E-Ink 刷新

---

**总体进度**: ~92%
- ✅ Qt6 + WebEngine 编译: 100%
- ✅ 应用功能: 100% (VNC 验证)
- ⏳ E-Ink 显示: 40% (理解了架构，但需要重新编译 Qt6 + 自研刷新)

**预计完成时间**:
- 保守估计: 2-3 天
- 乐观估计: 1-2 天

---

**下一步决策点**:
1. 是否现在开始重新编译 Qt6? (6-8 小时)
2. 还是先研究 DRM E-Ink 刷新机制? (2-4 小时)

**建议**: 先研究，再编译 - 避免盲目重新编译
