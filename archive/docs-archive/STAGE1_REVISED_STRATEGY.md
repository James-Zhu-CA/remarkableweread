# Stage 1 修订策略 - 基于 KOReader 和 qtfb-shim 分析

**日期**: 2025-11-18 05:30
**策略**: 🎯 **先跑通，再优化 - 使用 qtfb-shim 快速验证**
**设备确认**: reMarkable Paper Pro Move (chiappa)，分辨率 1696×954（已校准）

---

## 核心策略调整

### ❌ 放弃的路径
**直接猜测 ioctl 参数结构** - 效率低，成功率不确定

### ✅ 采用的新策略

**两步走方案**：
1. **短期目标** (1-2天): 使用 qtfb-shim 跑通应用
2. **长期目标** (可选): 用 ioctl logger 反推参数，实现直接调用

---

## 第一步：理解 KOReader 在 Paper Pro MOVE上的实际链路

### 任务 1.1: 确认 KOReader 的设备实现

**目标**: 确认 Paper Pro/Move 不直接调用 ioctl

**操作**:
```bash
cd /Users/jameszhu/AI_Projects/remarkableweread/weread-test/archived/koreader/koreader/koreader

# 查找 Paper Pro 相关代码
grep -r "RemarkablePaperPro\|is_rmpp\|Chiappa" frontend/device/
```

**预期发现**:
```lua
-- frontend/device/remarkable/device.lua (已读取)
if is_rmpp then
    if not is_qtfb_shimmed then
        error("reMarkable Paper Pro requires qtfb-shim")
    end
    return RemarkablePaperPro
end

-- 使用 framebuffer_mxcfb，但实际通过 qtfb-shim 拦截
self.screen = require("ffi/framebuffer_mxcfb"):new{...}
```

**结论**: ✅ 已确认 - KOReader 通过 qtfb-shim，不直接调用 ioctl

### 任务 1.2: 学习 KOReader 的刷新策略

**目标**: 提取可复用的刷新逻辑，而非底层实现

**重点文件**:
1. `frontend/ui/elements/screen_eink_opt_menu_table.lua` - 刷新选项
2. `ffi/framebuffer_mxcfb.lua` - 刷新抽象层

**可复用的设计思想**:
```lua
-- KOReader 的刷新抽象
fb:refreshPartial(x, y, w, h)  -- 局部刷新
fb:refreshFull()                -- 全屏刷新
fb:setFastMode(enabled)         -- 快速模式
fb:setFullRefreshInterval(n)   -- 每 N 次局刷做一次全刷
```

**转换为 Qt6 设计**:
```cpp
class EInkRefreshPolicy {
public:
    int fullRefreshInterval = 6;   // 每 6 次局刷做一次全刷
    bool fastMode = false;         // 快速模式（滚动/UI）
    bool enableDithering = true;   // 抖动

    enum Waveform {
        INIT = 0,      // 初始化（清屏）
        DU = 1,        // 快速单色
        GC16 = 2,      // 高质量灰度
        GL16 = 3,      // 文本优化
        A2 = 4         // 超快速黑白
    };
};

class AbstractEInkRefresher {
public:
    virtual void refreshPartial(const QRect &rect, Waveform wf = GL16) = 0;
    virtual void refreshFull(Waveform wf = GC16) = 0;
    virtual ~AbstractEInkRefresher() = default;
};
```

---

## 第二步：使用 qtfb-shim 快速跑通应用

### 任务 2.1: 理解 qtfb-shim 架构

**已知信息**:
```
Qt6 应用（使用 linuxfb）
  ↓ LD_PRELOAD
qtfb-shim.so (拦截 framebuffer 写入)
  ↓ Unix socket: /tmp/qtfb.sock
qtfb server (处理刷新)
  ↓ DRM ioctl (0xc02064b2, 0xc01064b3)
内核 DRM 驱动
  ↓
E-Ink 硬件
```

**设备上已有组件**:
- `/home/root/shims/qtfb-shim.so` ✅
- `/tmp/qtfb.sock` - 需要 qtfb server 运行

### 任务 2.2: 部署或复用 qtfb server

**选项 A: 复用 xochitl 的刷新机制**

xochitl 已经在运行，可能已经提供了刷新服务。

**验证方法**:
```bash
# 检查 /tmp/qtfb.sock 是否存在
ls -l /tmp/qtfb.sock

# 如果存在，检查谁在监听
lsof /tmp/qtfb.sock  # 可能需要安装 lsof
```

**选项 B: 部署独立 qtfb server (rm2fb)**

如果没有现成的 server，可以使用 rm2fb:
- GitHub: https://github.com/ddvk/remarkable2-framebuffer
- 或 Paper Pro 版本: https://github.com/asivery/rmpp-qtfb-shim

**步骤**:
1. 从项目获取预编译的 server binary
2. 部署到设备: `/home/root/qtfb-server`
3. 启动: `./qtfb-server &`
4. 验证 socket 创建

### 任务 2.3: 创建 Qt6 应用的 shim 实现

**版本 1: 最简测试** (验证 qtfb-shim 可用)

创建一个极简 Qt6 应用，只画一个矩形：

```cpp
// minimal_qt6_test.cpp
#include <QApplication>
#include <QWidget>
#include <QPainter>

class TestWidget : public QWidget {
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(100, 100, 400, 300, Qt::black);
        p.setPen(Qt::white);
        p.drawText(200, 200, "Qt6 + qtfb-shim Test");
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    TestWidget w;
    w.showFullScreen();
    return app.exec();
}
```

**启动脚本**:
```bash
#!/bin/sh
# test_qtfb_shim.sh

export QT_QPA_PLATFORM=linuxfb
export LD_PRELOAD=/home/root/shims/qtfb-shim.so

cd /home/root/weread/apps/weread-browser/bin
./minimal_qt6_test
    ```

    **预期结果**:
    - 如果成功：屏幕显示黑色矩形和白色文字
    - 证明 Qt6 + qtfb-shim 链路工作

### AppLoad 集成现状（2025-11-19）
- AppLoad 路径：`/home/root/xovi/exthome/appload/weread-browser`
- 启动脚本：`weread-appload.sh`（由 `weread-test/deploy/appload/weread-browser/weread-appload.sh` 更新，指向 `/home/root/weread/qt6` 运行时与 `/home/root/weread/apps/weread-browser/bin/weread-browser` 二进制）
- Manifest 已开启 `qtfb: true`，AppLoad 会自动分配 `QTFB_KEY` 并启动 qtfb server（创建 `/tmp/qtfb.sock`）
- 运行步骤：
  1. `ssh root@10.11.99.1 /home/root/xovi/start`（首次或系统更新后执行一次，确保 xovi/AppLoad 挂载生效）
  2. 在设备界面中打开 AppLoad，选择“微信读书”
  3. AppLoad 会在后台启动 qtfb server + weread 应用（若需调试，可同时 `ssh` 观察 `/tmp/qtfb.sock` 或日志）
- 如需重新部署脚本：`scp weread-test/deploy/appload/weread-browser/weread-appload.sh root@10.11.99.1:/home/root/xovi/exthome/appload/weread-browser/`

**版本 2: 抽象刷新接口**

```cpp
// eink_refresher.h
class AbstractEInkRefresher {
public:
    virtual void refreshPartial(const QRect &rect) = 0;
    virtual void refreshFull() = 0;
    virtual ~AbstractEInkRefresher() = default;
};

// shim_refresher.h (依赖 qtfb-shim)
class ShimRefresher : public AbstractEInkRefresher {
public:
    void refreshPartial(const QRect &rect) override {
        // qtfb-shim 自动处理，我们只需要确保
        // QWidget::update() 被调用
        // shim 会拦截 framebuffer 写入
    }

    void refreshFull() override {
        // 触发全屏重绘
    }
};

// 未来的直接 ioctl 实现
class DrmRefresher : public AbstractEInkRefresher {
private:
    int drm_fd;

public:
    DrmRefresher() {
        drm_fd = open("/dev/dri/card0", O_RDWR);
    }

    void refreshPartial(const QRect &rect) override {
        // 直接调用我们发现的 ioctl
        struct eink_config_params config = {...};
        ioctl(drm_fd, 0xc02064b2, &config);
        // ...
        ioctl(drm_fd, 0xc01064b3, &refresh);
    }

    void refreshFull() override {
        refreshPartial(QRect(0, 0, 1620, 2160));
    }
};
```

**应用集成**:
```cpp
// main.cpp
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // 根据配置选择刷新实现
    std::unique_ptr<AbstractEInkRefresher> refresher;

    if (qgetenv("USE_SHIM") == "1") {
        refresher = std::make_unique<ShimRefresher>();
    } else {
        refresher = std::make_unique<DrmRefresher>();
    }

    // WebView 使用抽象接口
    WebBrowser browser(std::move(refresher));
    browser.showFullScreen();

    return app.exec();
}
```

---

## 第三步（可选）：反推 ioctl 参数

### 任务 3.1: 编写 ioctl logger

**目标**: Hook ioctl 调用，dump 参数内容

**ioctl_logger.c**:
```c
#define _GNU_SOURCE
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static int (*real_ioctl)(int, unsigned long, ...) = NULL;

static void hexdump(const char *desc, const void *addr, int len) {
    const unsigned char *pc = (const unsigned char *)addr;

    fprintf(stderr, "[ioctl_logger] %s (%d bytes):\n", desc, len);
    for (int i = 0; i < len; i++) {
        if (i % 16 == 0)
            fprintf(stderr, "  %04x: ", i);
        fprintf(stderr, "%02x ", pc[i]);
        if (i % 16 == 15 || i == len - 1)
            fprintf(stderr, "\n");
    }
}

int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    void *argp = va_arg(args, void *);
    va_end(args);

    if (!real_ioctl) {
        real_ioctl = dlsym(RTLD_NEXT, "ioctl");
    }

    // 解析 ioctl 号
    unsigned int dir = (request >> 30) & 0x3;
    unsigned int size = (request >> 16) & 0x3fff;
    unsigned int type = (request >> 8) & 0xff;
    unsigned int nr = request & 0xff;

    // 只关注 DRM ioctl (type='d'=0x64)
    if (type == 0x64) {
        fprintf(stderr, "\n[ioctl_logger] fd=%d, cmd=0x%08lx\n", fd, request);
        fprintf(stderr, "  dir=%s, size=%u, type='%c', nr=%u\n",
                dir == 3 ? "RW" : (dir == 2 ? "R" : (dir == 1 ? "W" : "NONE")),
                size, (char)type, nr);

        // 特别关注我们发现的两个 ioctl
        if (request == 0xc02064b2) {
            fprintf(stderr, "  *** CONFIG IOCTL (32 bytes) ***\n");
            if (argp && size > 0) {
                hexdump("Config params", argp, size);
            }
        } else if (request == 0xc01064b3) {
            fprintf(stderr, "  *** REFRESH IOCTL (16 bytes) ***\n");
            if (argp && size > 0) {
                hexdump("Refresh params", argp, size);
            }
        }
    }

    int ret = real_ioctl(fd, request, argp);

    if (type == 0x64 && ret < 0) {
        fprintf(stderr, "  [ERROR] ioctl failed: %s\n", strerror(errno));
    }

    return ret;
}
```

**编译**:
```bash
# 在 Docker 容器中
docker exec qt6-arm-builder bash -c 'cd /tmp && \
  gcc -shared -fPIC -o ioctl_logger.so ioctl_logger.c -ldl'

# 复制到设备
docker cp qt6-arm-builder:/tmp/ioctl_logger.so /tmp/
sshpass -p 'QpEXvfq2So' scp -o StrictHostKeyChecking=no \
  /tmp/ioctl_logger.so root@10.11.99.1:/home/root/
```

### 任务 3.2: 追踪真正发 ioctl 的进程

**选项 A: 追踪 qtfb server**

如果有独立的 qtfb server:
```bash
# 重启 server 并带上 logger
systemctl stop qtfb-server  # 如果有 systemd 服务
LD_PRELOAD=/home/root/ioctl_logger.so /home/root/qtfb-server 2>&1 | tee /tmp/ioctl.log
```

**选项 B: 追踪 xochitl**

```bash
# 临时停止 xochitl
systemctl stop xochitl

# 带 logger 启动
LD_PRELOAD=/home/root/ioctl_logger.so /usr/bin/xochitl --system 2>&1 | tee /tmp/xochitl_ioctl.log &

# 触摸屏幕，观察刷新
# 然后检查日志
cat /tmp/xochitl_ioctl.log | grep "CONFIG\|REFRESH"
```

**选项 C: 追踪自己的测试程序**

创建一个简单的程序，通过 qtfb-shim 刷新：
```bash
LD_PRELOAD="/home/root/ioctl_logger.so /home/root/shims/qtfb-shim.so" \
  /tmp/minimal_qt6_test 2>&1 | tee /tmp/test_ioctl.log
```

**预期输出**:
```
[ioctl_logger] fd=5, cmd=0xc02064b2
  dir=RW, size=32, type='d', nr=178
  *** CONFIG IOCTL (32 bytes) ***
  Config params (32 bytes):
    0000: 00 00 00 00 00 00 00 00 54 06 00 00 70 08 00 00
    0010: 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

[ioctl_logger] fd=5, cmd=0xc01064b3
  dir=RW, size=16, type='d', nr=179
  *** REFRESH IOCTL (16 bytes) ***
  Refresh params (16 bytes):
    0000: 24 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

从 hexdump 可以直接看到参数结构！

---

## 实施时间表

### 阶段 1: qtfb-shim 快速验证 (4-6小时)

**Day 1 上午 (2-3小时)**:
- ✅ 理解 KOReader Paper Pro 实现（已完成）
- ⏳ 检查设备上 qtfb server 状态
- ⏳ 如需要，部署 qtfb server

**Day 1 下午 (2-3小时)**:
- ⏳ 创建最简 Qt6 测试程序
- ⏳ 使用 qtfb-shim 验证显示和刷新
- ⏳ 集成到现有 WebView 应用

**预期结果**: Qt6 WebView 应用能通过 qtfb-shim 正常显示和刷新

### 阶段 2: 抽象刷新接口 (2-3小时)

**Day 2 上午**:
- ⏳ 设计 AbstractEInkRefresher 接口
- ⏳ 实现 ShimRefresher（基于 qtfb-shim）
- ⏳ 重构应用使用抽象接口

### 阶段 3（可选）: ioctl logger 反推 (4-6小时)

**Day 2 下午** (如果需要直接 ioctl 实现):
- ⏳ 编写和部署 ioctl_logger.so
- ⏳ 追踪 xochitl 或 qtfb server
- ⏳ 分析 hexdump，重建参数结构
- ⏳ 实现 DrmRefresher（直接 ioctl）
- ⏳ 测试并切换到直接实现

---

## 成功标准

### 阶段 1 成功标准
- [ ] Qt6 应用通过 qtfb-shim 正常显示
- [ ] 触摸、滚动能触发屏幕刷新
- [ ] WebView 内容能正常加载和交互

### 阶段 2 成功标准
- [ ] 代码使用抽象刷新接口
- [ ] 可以通过环境变量切换实现
- [ ] 不同实现功能一致

### 阶段 3 成功标准（可选）
- [ ] ioctl logger 成功捕获参数
- [ ] 能从 hexdump 重建结构体
- [ ] DrmRefresher 实现能独立工作
- [ ] 性能和功能与 shim 版本相当

---

## 风险和备选方案

### 风险 1: qtfb server 不可用

**缓解**:
- 部署 rm2fb 或 rmpp-qtfb-shim
- 或继续使用 qtfb-shim 方案（已验证可行）

### 风险 2: qtfb-shim 与 Qt6 不兼容

**缓解**:
- KOReader 已证明可行
- Qt5 和 Qt6 的 linuxfb 插件应该兼容
- 最坏情况：降级到 Qt5

### 风险 3: ioctl logger 无法获取完整参数

**缓解**:
- 不依赖此方法作为主要路径
- qtfb-shim 方案已足够
- 直接 ioctl 仅作为性能优化

---

## 下一步行动

**立即执行**:
1. 检查设备上是否有 qtfb server 或 socket
2. 创建最简 Qt6 测试程序
3. 使用 qtfb-shim 验证

## TODO - ioctl logger 路线（Phase A 红利）
- [x] 在设备上确认 `/dev/dri/card0` 可用并选择 hook 目标进程（xochitl 或 qtfb server）
- [x] 在 `qt6-arm-builder` 中编译 `ioctl_logger.so`（已备源码：`weread-test/ioctl_logger.c`，脚本：`weread-test/scripts/build-ioctl-logger.sh`）
- [x] 部署 logger 到设备并以 `LD_PRELOAD` 方式运行 xochitl│qtfb-server，触发真实刷新（日志见 `docs-archive/logs/xochitl_ioctl_logger_20251119.log`）
- [ ] 收集 hexdump，按调用顺序 `config ioctl → drmModeAddFB → refresh ioctl` 标注字段，重建结构体（需扩展 logger 以捕获更多命令）
- [ ] 回填参数结构到 Stage 1 文档，并更新驱动调用实现（替换现有猜测）

## 过期/限定结论（请勿混用）
- mxcfb `/dev/fb0` 路径仅适用于 rM1/rM2，Paper Pro Move 为 DRM-only，现有 `EInkRefresher`（mxcfb）代码仅做参考暂不适用。

**文档链接**:
- [KOReader 设备定义分析](../weread-test/archived/koreader/koreader/koreader/frontend/device/remarkable/device.lua)
- [qtfb-shim 运行脚本](../weread-test/run-with-qtfb-shim.sh)
- [当前状态总结](STAGE1_FINAL_STATUS.md)
- [ioctl 捕获记录 2025-11-19](STAGE1_IOCTL_CAPTURE_20251119.md)
- [ioctl 字段拆解草案](STAGE1_IOCTL_FIELD_INFERENCE.md)

---

**修订日期**: 2025-11-18 05:30
**状态**: 策略已更新，准备实施阶段 1
