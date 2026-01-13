# E-Ink 刷新问题解决方案

**时间**: 2025-11-22 18:40 UTC
**问题**: WeRead 运行正常，有网络连接，但白屏无显示

---

## 根本原因分析

### 问题定位

通过深入代码分析发现：

1. **qtfb-shim** 拦截 MXCFB_SEND_UPDATE ioctl，但**不直接操作硬件**
   - 代码位置：[rm-appload/shim/src/fb-shim.cpp:74-94](file:///Users/jameszhu/AI_Projects/remarkableweread/rm-appload/shim/src/fb-shim.cpp:74)
   - 行为：调用 `clientConnection->sendPartialUpdate()` 发送消息到 qtfb-server

2. **qtfb-server 不支持 E-Ink 刷新**
   - 代码位置：[rm-appload/src/qtfb/FBController.cpp:32-56](file:///Users/jameszhu/AI_Projects/remarkableweread/rm-appload/src/qtfb/FBController.cpp:32)
   - 行为：只将共享内存画到 QML Window 的 QPainter
   - **问题**：没有任何代码调用 E-Ink 驱动的 ioctl！

3. **qtfb-server 设计为 PC 模拟器**
   - 期望在 PC 屏幕上显示 QML Window
   - 在设备上运行时使用 `QT_QPA_PLATFORM=offscreen`
   - **结果**：QML Window 不显示，E-Ink 不刷新

### 数据流图

```
┌─────────────────┐
│  WeRead         │
│  (WebEngine)    │
└────────┬────────┘
         │ 渲染到共享内存
         ↓
┌────────────────────────────┐
│  qtfb-shim (LD_PRELOAD)   │
│  - 拦截 open("/dev/fb0")   │
│  - 拦截 ioctl(MXCFB_...)  │
└────────┬───────────────────┘
         │ sendPartialUpdate()
         │ via UNIX socket
         ↓
┌────────────────────────────┐
│  qtfb-server              │
│  - 接收更新请求            │
│  - 画到 QPainter          │
│  - ❌ 不刷新 E-Ink         │
└────────┬───────────────────┘
         │
         ↓
    QML Window
  (offscreen mode)
         ↓
     nowhere!
```

**缺失的环节**：qtfb-server → E-Ink Driver

---

## 官方证实

根据 [rmkit.dev](https://rmkit.dev/news/rm2-status/) 文档：

> **"On the reMarkable Paper Pro, refresh is currently controlled by xochitl and will change in future after qtfb and shim gets ability to control it."**

**确认**：
- ❌ 当前版本的 qtfb-server **不支持 E-Ink 刷新**
- ✅ E-Ink 刷新由 xochitl 控制
- ⚠️ 但我们的应用替换了 qtfb socket，绕过了 xochitl

---

## 解决方案

### 方案：修改 qtfb-server 添加 E-Ink 刷新支持

**基于**：[src/app/eink_refresh.cpp](file:///Users/jameszhu/AI_Projects/remarkableweread/src/app/eink_refresh.cpp:1) 的实现

### 修改内容

#### 1. 在 FBController.h 添加成员变量

```cpp
private:
    int einkFd = -1;            // /dev/fb0 file descriptor
    int einkWidth = 1696;       // Screen width
    int einkHeight = 954;       // Screen height
    unsigned int einkMarker = 1; // Update marker counter

    void sendEinkUpdate(const QRect& rect);
```

#### 2. 在 FBController.cpp 构造函数中初始化

```cpp
FBController::FBController(QQuickItem *parent) : QQuickPaintedItem(parent) {
    // ... existing code ...

    // Initialize E-Ink refresh
    einkFd = open("/dev/fb0", O_RDWR);
    if (einkFd >= 0) {
        fb_var_screeninfo vinfo;
        if (ioctl(einkFd, FBIOGET_VSCREENINFO, &vinfo) == 0) {
            einkWidth = vinfo.xres;
            einkHeight = vinfo.yres;
            qDebug() << "[EINK] E-Ink initialized:" << einkWidth << "x" << einkHeight;
        }
    }
}
```

#### 3. 在 markedUpdate() 中触发 E-Ink 刷新

```cpp
void FBController::markedUpdate(const QRect &rect) {
    // ... existing update() call ...

    // NEW: Trigger E-Ink refresh
    if (einkFd >= 0 && _active) {
        sendEinkUpdate(rect);
    }
}
```

#### 4. 实现 sendEinkUpdate() 方法

```cpp
void FBController::sendEinkUpdate(const QRect& rect) {
    mxcfb_update_data updateData{};

    updateData.update_region.top = rect.top();
    updateData.update_region.left = rect.left();
    updateData.update_region.width = rect.width();
    updateData.update_region.height = rect.height();

    // Waveform mode based on _refreshMode
    int waveform = 2; // GC16 (high quality)
    if (_refreshMode == REFRESH_MODE_FAST) waveform = 1; // DU
    if (_refreshMode == REFRESH_MODE_ANIMATE) waveform = 6; // A2

    updateData.waveform_mode = waveform;
    updateData.update_mode = UPDATE_MODE_PARTIAL;
    updateData.update_marker = einkMarker++;
    updateData.temp = 0x0018;

    ioctl(einkFd, MXCFB_SEND_UPDATE, &updateData);
}
```

#### 5. 在析构函数中清理

```cpp
FBController::~FBController() {
    if (einkFd >= 0) {
        close(einkFd);
    }
    qtfb::management::unregisterController(_framebufferID);
}
```

---

## 实施步骤

### 1. 备份原始文件

```bash
cd /Users/jameszhu/AI_Projects/remarkableweread/rm-appload/src/qtfb
cp FBController.h FBController.h.backup
cp FBController.cpp FBController.cpp.backup
```

### 2. 应用补丁

参考：[FBController-eink-patch.cpp](file:///Users/jameszhu/AI_Projects/remarkableweread/rm-appload/src/qtfb/FBController-eink-patch.cpp:1)

### 3. 重新编译 qtfb-server

```bash
# 在 Docker 容器中编译
docker exec qt6-arm-builder bash -c "
  cd /workspace/rm-appload/build &&
  cmake .. &&
  make qtfb-server
"

# 复制编译结果
docker cp qt6-arm-builder:/workspace/rm-appload/build/qtfb-server /tmp/qtfb-server-eink
```

### 4. 部署到设备

```bash
sshpass -p 'QpEXvfq2So' scp /tmp/qtfb-server-eink root@10.11.99.1:/home/root/weread/qtfb-server
```

### 5. 测试

```bash
# 重启 WeRead
sshpass -p 'QpEXvfq2So' ssh root@10.11.99.1 'killall weread-browser qtfb-server'
# 启动应用（使用 AppLoad 或手动启动）
```

---

## 预期效果

修改后的数据流：

```
WeRead → qtfb-shim → qtfb-server → QML Window (offscreen)
                           ↓
                      E-Ink ioctl
                           ↓
                     /dev/fb0 驱动
                           ↓
                      物理屏幕 ✅
```

**预期结果**：
- ✅ 共享内存内容写入到 E-Ink
- ✅ 屏幕显示 WeRead 内容
- ✅ 触摸输入正常工作

---

## 替代方案（如果主方案不work）

### 方案 B：让 qtfb-shim 直接刷新 E-Ink

**修改**：`rm-appload/shim/src/fb-shim.cpp`

```cpp
int fbShimIoctl(int fd, unsigned long request, char *ptr) {
    if (fd == shmFD && request == MXCFB_SEND_UPDATE) {
        // NEW: 同时调用真实的 ioctl
        int realFd = open("/dev/fb0", O_RDWR);
        if (realFd >= 0) {
            ioctl(realFd, request, ptr);
            close(realFd);
        }

        // 原有逻辑：发送到 qtfb-server
        clientConnection->sendPartialUpdate(...);
        return 0;
    }
}
```

**优点**：
- 更简单，只修改 shim
- 不需要重新编译 qtfb-server

**缺点**：
- 每次刷新都要 open/close fd（性能开销）
- 绕过了 qtfb-server 的管理

---

## 参考资料

### 文档
- [reMarkable Framebuffer Overview](https://github.com/canselcik/libremarkable/wiki/Framebuffer-Overview)
- [rmkit E-Ink development notes](https://rmkit.dev/eink-dev-notes/)
- [reMarkable 2 Status](https://rmkit.dev/news/rm2-status/)

### 代码参考
- [libremarkable mxcfb.h](https://github.com/canselcik/libremarkable/blob/master/reference-material/mxcfb.h)
- [FBInk - E-Ink framebuffer library](https://github.com/NiLuJe/FBInk)
- [KOReader Paper Pro support](https://github.com/koreader/koreader/pull/13620)

### 本地代码
- E-Ink 刷新实现：[src/app/eink_refresh.cpp](file:///Users/jameszhu/AI_Projects/remarkableweread/src/app/eink_refresh.cpp:1)
- qtfb-shim ioctl 处理：[rm-appload/shim/src/fb-shim.cpp](file:///Users/jameszhu/AI_Projects/remarkableweread/rm-appload/shim/src/fb-shim.cpp:72)
- FBController 绘制：[rm-appload/src/qtfb/FBController.cpp](file:///Users/jameszhu/AI_Projects/remarkableweread/rm-appload/src/qtfb/FBController.cpp:32)

---

**下一步**: 实施 qtfb-server 修改并重新编译测试
