# E-Ink 刷新集成 - 三阶段执行计划

**创建时间**: 2025-11-18 01:30
**策略**: 渐进式验证，避免多线踩坑

---

## 🎯 总体策略

**核心原则**: 先搞定"刷新指令"，再集成到应用，最后才考虑重编 Qt

```
Stage 1: 找出 ioctl 刷新指令 (2-4 小时)
   ↓ 验证成功
Stage 2: 挂到现有 Qt6 应用 (1-2 小时)
   ↓ 如果 linuxfb 不配合
Stage 3 (可选): 重编 Qt6 启用 eglfs (6-8 小时)
```

---

## Stage 1: 精确找出刷新指令 ⏳ 当前阶段

**目标**: 做一个 100 行以内的 C 程序，能让屏幕刷新一次

**不需要**:
- ❌ 不动 Qt
- ❌ 不动应用
- ❌ 不重新编译任何东西

**只需要**:
- ✅ 找到正确的 ioctl 调用
- ✅ 确认屏幕有响应（哪怕闪一下）

---

### 1.1 strace 追踪 xochitl ⏳ 进行中

**步骤**:

```bash
# 1. 在设备上启动 xochitl (如果没运行)
systemctl start xochitl

# 2. 启动 strace 追踪
strace -f -e trace=ioctl -o /tmp/xochitl.ioctl.log -p $(pidof xochitl)

# 3. 在界面上做以下动作（分别记录时间）:
#    - T1: 进入主界面（一次全刷）
#    - T2: 画一笔 / 翻一页（典型局刷）
#    - T3: 屏幕休眠 / 唤醒（如有）

# 4. Ctrl+C 停止 strace

# 5. 查看日志
cat /tmp/xochitl.ioctl.log
```

**预期输出**:
- DRM ioctl 调用
- 自定义 E-Ink ioctl (如果有)
- ioctl 编号和参数

**执行状态**: ⬜ 待开始

---

### 1.2 对应到内核头文件

**目标**: 找到 ioctl 编号对应的宏定义和结构体

**步骤**:

```bash
# 在设备上搜索 ioctl 编号
# 假设 strace 显示 ioctl(fd, 0xc0184500, ...)

# 搜索方式 1: 在系统头文件中搜索
grep -r "0xc0184500" /usr/include /usr/src 2>/dev/null

# 搜索方式 2: 搜索可能的宏名
grep -r "EINK\|EPDC\|MXCFB" /usr/include/linux/*.h 2>/dev/null

# 搜索方式 3: 查看 DRM 相关头文件
ls /usr/include/drm/*.h
grep -i "update\|refresh" /usr/include/drm/*.h
```

**预期发现**:
- 类似 `MXCFB_SEND_UPDATE` 的宏
- 或 DRM 标准 ioctl: `DRM_IOCTL_MODE_*`
- 对应的结构体定义

**执行状态**: ⬜ 待开始

---

### 1.3 编写最小 C demo

**目标**: 100 行以内的 C 程序，运行后屏幕有响应

**代码框架** (`eink_refresh_demo.c`):

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

// 根据 1.2 找到的定义填充
// 例如:
// #include <linux/mxcfb.h>
// 或者手动定义 ioctl 编号

#define DEVICE_PATH "/dev/dri/card0"  // 或 /dev/fb0

// 根据 strace 结果填充
// 示例 (需要替换):
struct eink_update_data {
    unsigned int x;
    unsigned int y;
    unsigned int width;
    unsigned int height;
    unsigned int waveform_mode;
    unsigned int update_mode;
    // ... 其他字段
};

#define EINK_REFRESH_IOCTL 0xc0184500  // 替换为实际值

int main() {
    printf("Opening device: %s\n", DEVICE_PATH);
    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    printf("Device opened, fd=%d\n", fd);

    // 准备刷新参数 (全屏刷新)
    struct eink_update_data update;
    memset(&update, 0, sizeof(update));
    update.x = 0;
    update.y = 0;
    update.width = 1620;   // Paper Pro 分辨率
    update.height = 2160;
    update.waveform_mode = 2;  // GC16 (根据 Oxide 的值)
    update.update_mode = 1;    // Full update

    printf("Sending refresh ioctl...\n");
    int ret = ioctl(fd, EINK_REFRESH_IOCTL, &update);
    if (ret < 0) {
        perror("ioctl");
        close(fd);
        return 1;
    }

    printf("Refresh ioctl sent successfully!\n");
    printf("Check the screen - did it flash/refresh?\n");

    close(fd);
    return 0;
}
```

**编译**:
```bash
# 在 Docker ARM64 容器中编译
docker exec qt6-arm-builder bash -c 'cd /tmp && gcc -o eink_refresh_demo eink_refresh_demo.c'

# 或在设备上直接编译 (如果有 gcc)
gcc -o eink_refresh_demo eink_refresh_demo.c
```

**测试**:
```bash
# 传输到设备
scp eink_refresh_demo root@10.11.99.1:/tmp/

# 在设备上运行
ssh root@10.11.99.1 '/tmp/eink_refresh_demo'

# 观察:
# ✅ 成功: 屏幕闪一下 / 刷新
# ❌ 失败: 没反应 / 报错 → 调整参数重试
```

**成功标志**:
- ✅ 屏幕有任何可见的变化（刷新、闪烁、显示变化）
- ✅ ioctl 返回成功（ret == 0）

**执行状态**: ⬜ 待开始

---

### Stage 1 检查点

**必须满足**:
- ✅ 找到了正确的 ioctl 编号
- ✅ 找到了对应的结构体定义
- ✅ C demo 运行后屏幕有响应

**如果通过** → 进入 Stage 2
**如果失败** → 调整参数，或寻求社区帮助

---

## Stage 2: 挂到现有 Qt6 应用 ⏳ 待开始

**前提**: Stage 1 成功

**目标**: 在现有 Qt6 (linuxfb/vnc) 上集成 E-Ink 刷新

**不需要**:
- ❌ 不重新编译 Qt6
- ❌ 不改显示平台

**只需要**:
- ✅ 封装 Stage 1 的代码
- ✅ 在应用关键点调用

---

### 2.1 方案选择

**方案 A: 应用内直接调用** (推荐)

```cpp
// eink_drm_refresher.h
class EInkDRMRefresher {
public:
    static bool init();
    static void fullRefresh();
    static void partialRefresh(const QRect &rect);
    static void cleanup();
private:
    static int s_fd;
};

// eink_drm_refresher.cpp
#include "eink_drm_refresher.h"
// ... Stage 1 的 ioctl 代码

// main.cpp
#include "eink_drm_refresher.h"

class PenFriendlyWebView : public QWebEngineView {
    QTimer *m_refreshTimer;
protected:
    void paintEvent(QPaintEvent *event) override {
        QWebEngineView::paintEvent(event);
        scheduleRefresh();
    }

    void scheduleRefresh() {
        if (!m_refreshTimer->isActive()) {
            m_refreshTimer->start();
        }
    }

    void performRefresh() {
        EInkDRMRefresher::fullRefresh();  // ← Stage 1 的代码
    }
};

int main() {
    QApplication app;

    if (EInkDRMRefresher::init()) {
        qDebug() << "E-Ink DRM refresh enabled";
    }

    // ...
}
```

**方案 B: Helper 进程模式** (备选)

```bash
# helper 进程 (eink-refresh-daemon)
while true; do
    read cmd  # 从 Unix socket 或 stdin 读取命令
    case $cmd in
        "full") /tmp/eink_refresh_demo ;;
        "partial") /tmp/eink_refresh_demo --partial $rect ;;
    esac
done

# Qt 应用调用
system("echo 'full' > /var/run/eink-refresh.fifo");
```

**推荐**: 方案 A - 更简单直接

---

### 2.2 集成步骤

1. **复制 Stage 1 代码**:
   ```bash
   cp eink_refresh_demo.c weread-test/app/eink_drm_refresher.cpp
   ```

2. **封装成 C++ 类**:
   - 参考之前的 `eink_refresh.cpp`
   - 但使用 Stage 1 找到的正确 ioctl

3. **在 main.cpp 集成**:
   - 初始化: `EInkDRMRefresher::init()`
   - 刷新调用: 在 `paintEvent()` 或 `loadFinished()` 后

4. **编译测试**:
   ```bash
   cd weread-test/app/build
   make
   scp weread-browser root@10.11.99.1:/home/root/weread/apps/weread-browser/bin/
   ```

5. **在设备上测试**:
   ```bash
   # 使用 VNC 或 offscreen 模式
   export QT_QPA_PLATFORM=vnc
   ./apps/weread-browser/bin/weread-browser

   # VNC 连接后，观察物理屏幕是否刷新
   ```

---

### Stage 2 检查点

**必须满足**:
- ✅ VNC/offscreen 模式下应用正常运行
- ✅ 物理屏幕能看到刷新（哪怕效果不完美）
- ✅ 网页加载后能在 E-Ink 上显示

**如果通过** → 优化刷新策略，项目完成！
**如果失败** → 可能需要 Stage 3

---

## Stage 3 (可选): 重编 Qt6 启用 eglfs ⏳ 按需执行

**触发条件** (任意一个):
- ❌ Stage 2 发现 linuxfb 根本拿不到正确 buffer
- ❌ 需要 GPU 加速 / 多 plane 合成
- ❌ 需要更好的性能

**如果不满足上述条件** → 不需要执行 Stage 3

---

### 3.1 重新编译 Qt6

```bash
cd /workspace/qt6-src
mkdir build-qt6-eglfs && cd build-qt6-eglfs

cmake -G Ninja ../qt6-src \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/opt/weread/qt6-eglfs \
  -DQT_BUILD_EXAMPLES=OFF \
  -DQT_BUILD_TESTS=OFF \
  -DFEATURE_webengine=ON \
  -DFEATURE_opengl=ON \            # ✅
  -DFEATURE_eglfs=ON \              # ✅
  -DFEATURE_eglfs_kms=ON \          # ✅
  -DFEATURE_eglfs_gbm=ON \          # ✅
  -DFEATURE_linuxfb=ON              # 保留

ninja -j$(nproc)  # 6-8 小时
```

### 3.2 迁移刷新代码

**无需改动** - Stage 2 的刷新代码可以直接使用：
- ioctl 调用逻辑不变
- 只是 Qt 平台从 linuxfb 变成 eglfs

### 3.3 测试

```bash
export QT_QPA_PLATFORM=eglfs
export QT_QPA_EGLFS_KMS_CONFIG=/path/to/eglfs.json  # 如果需要
./apps/weread-browser/bin/weread-browser
```

---

## 📋 执行时间表

| Stage | 预估时间 | 前提条件 | 状态 |
|-------|----------|----------|------|
| Stage 1.1 | 30 分钟 - 1 小时 | strace 可用 | ⬜ 待开始 |
| Stage 1.2 | 30 分钟 - 1 小时 | 找到 strace 日志 | ⬜ 待开始 |
| Stage 1.3 | 1-2 小时 | 找到 ioctl 定义 | ⬜ 待开始 |
| **Stage 1 总计** | **2-4 小时** | - | **⏳ 当前** |
| Stage 2 | 1-2 小时 | Stage 1 成功 | ⬜ 待开始 |
| Stage 3 (可选) | 6-8 小时 | Stage 2 失败 | ⬜ 按需 |

**最佳情况**: 3-6 小时 (Stage 1 + 2)
**最坏情况**: 9-14 小时 (Stage 1 + 2 + 3)

---

## 🎯 立即行动 - Stage 1.1

### 步骤 1: 准备 strace 工具

```bash
# 检查 Docker 容器中是否有 strace
docker exec qt6-arm-builder which strace

# 如果有，复制到设备
docker cp qt6-arm-builder:/usr/bin/strace .
scp strace root@10.11.99.1:/tmp/strace
ssh root@10.11.99.1 'chmod +x /tmp/strace'
```

### 步骤 2: 启动 xochitl 并追踪

```bash
# SSH 到设备
ssh root@10.11.99.1

# 启动 xochitl (如果没运行)
systemctl start xochitl
sleep 3

# 找到 PID
XOCHITL_PID=$(ps | grep xochitl | grep -v grep | awk '{print $1}')
echo "xochitl PID: $XOCHITL_PID"

# 启动 strace
/tmp/strace -f -e trace=ioctl -o /tmp/xochitl.ioctl.log -p $XOCHITL_PID

# 现在在界面上操作:
# - 进入主界面
# - 画一笔
# - 翻一页
# 等待 10-20 秒

# Ctrl+C 停止 strace

# 查看日志
head -100 /tmp/xochitl.ioctl.log
```

### 步骤 3: 分析日志

```bash
# 筛选 DRM 相关的 ioctl
grep "dri\|DRM" /tmp/xochitl.ioctl.log

# 查看最频繁的 ioctl
cat /tmp/xochitl.ioctl.log | grep -o "ioctl([0-9]*, 0x[0-9a-f]*" | sort | uniq -c | sort -rn | head -20
```

---

**下一步**: 执行 Stage 1.1 - 获取 strace 日志

**预期成果**: 找到 E-Ink 刷新的 ioctl 调用
