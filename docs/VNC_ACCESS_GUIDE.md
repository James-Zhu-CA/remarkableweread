# VNC 访问指南 - 查看 WeRead Browser 输出

## 当前状态

✅ **应用成功运行** - weread-browser 在 VNC 后端正常工作
⚠️ **E-Ink 物理显示** - 需要集成 E-Ink 刷新机制 (Phase E)

## 快速连接 VNC

### 方法 1: 使用 VNC Viewer (推荐)

1. **下载 VNC Viewer**
   - macOS: [RealVNC Viewer](https://www.realvnc.com/en/connect/download/viewer/)
   - 或使用内置的 Screen Sharing.app

2. **连接到设备**
   ```
   地址: 10.11.99.1:5900
   端口: 5900
   ```

3. **查看应用界面**
   - 您应该能看到微信读书网页界面
   - 分辨率: 1620x2160

### 方法 2: 使用 macOS 内置 Screen Sharing

```bash
open vnc://10.11.99.1:5900
```

### 方法 3: 命令行 (验证连接)

```bash
# 检查 VNC 端口是否开放
nc -zv 10.11.99.1 5900

# 使用 SSH 端口转发 (更安全)
ssh -L 5901:localhost:5900 root@10.11.99.1
# 然后连接到 localhost:5901
```

##启动 WeRead Browser (VNC 模式)

### 停止当前测试程序

```bash
ssh root@10.11.99.1  # 密码: QpEXvfq2So
killall minimal_test weread-browser
```

### 启动完整浏览器

```bash
cd /home/root/weread

export LD_LIBRARY_PATH=/home/root/weread/qt6/lib
export QT_PLUGIN_PATH=/home/root/weread/qt6/plugins
export QML2_IMPORT_PATH=/home/root/weread/qt6/qml

# VNC 显示配置
export QT_QPA_PLATFORM=vnc
export QT_QPA_VNC_SIZE=1620x2160
export QT_QPA_VNC_PORT=5900

# WebEngine 配置
export QTWEBENGINE_DISABLE_SANDBOX=1
export QTWEBENGINE_CHROMIUM_FLAGS="\
--disable-gpu \
--disable-gpu-compositing \
--no-sandbox \
--single-process"

# 后台运行
./apps/weread-browser/bin/weread-browser > /tmp/weread-vnc.log 2>&1 &
```

### 查看日志

```bash
tail -f /tmp/weread-vnc.log
```

## 预期结果

通过 VNC 连接后，您应该看到：

1. **加载屏幕** (0-30秒)
   - Qt 窗口初始化
   - 白色背景

2. **微信读书首页** (30-60秒)
   - https://weread.qq.com/web
   - 扫码登录界面
   - 或登录表单

3. **完整网页** (60秒后)
   - 页面完全加载
   - 可以看到书籍列表或登录二维码

## 故障排查

### 问题 1: 无法连接到 VNC

**检查 VNC 服务是否运行**:
```bash
ssh root@10.11.99.1
ps | grep minimal_test
netstat -tln | grep 5900
```

**重启应用**:
```bash
killall minimal_test
cd /home/root/weread
export LD_LIBRARY_PATH=/home/root/weread/qt6/lib
export QT_PLUGIN_PATH=/home/root/weread/qt6/plugins
export QT_QPA_PLATFORM=vnc
export QT_QPA_VNC_SIZE=1620x2160
./apps/minimal_test &
```

### 问题 2: VNC 显示黑屏

**原因**: 应用可能还在加载

**解决**: 等待 30-60 秒，检查日志:
```bash
cat /tmp/minimal_test_output.log
cat /tmp/weread-vnc.log
```

### 问题 3: 网页加载失败

**检查网络连接**:
```bash
ping -c 3 weread.qq.com
curl -I https://weread.qq.com/web
```

**检查 WebEngine 进程**:
```bash
ps | grep QtWebEngine
```

## 下一步：测试完整功能

### 1. 验证页面加载

- [ ] 连接到 VNC
- [ ] 看到微信读书首页
- [ ] 页面元素正常显示

### 2. 测试交互 (VNC 支持鼠标点击)

虽然通过 VNC，您可以用鼠标模拟触摸：
- [ ] 点击链接
- [ ] 滚动页面
- [ ] 测试登录流程

### 3. 验证扫码登录

- [ ] 看到二维码
- [ ] 用微信扫码
- [ ] 确认登录成功

## Phase E: 集成 E-Ink 刷新 (下一阶段)

当前 VNC 方案是**临时解决方案**，用于验证应用功能。要在物理 E-Ink 屏幕上显示，需要：

### 需要做的工作

1. **研究 reMarkable E-Ink 驱动**
   - 查找 ioctl 命令
   - 理解刷新机制

2. **适配 EPFramebuffer (两种方案)**

   **方案 A: 最小集成** (推荐，快速)
   - 提取 Oxide EPFramebuffer 的核心刷新代码
   - 创建独立的 C++ 文件
   - 在 WebEngineView 的 paintEvent 中调用刷新
   - 预计工作量: 2-4 小时

   **方案 B: 完整 Qt 插件**
   - 将 EPFramebuffer 完全适配到 Qt6
   - 创建自定义 QPA 平台插件
   - 实现完整的 E-Ink 优化 (波形选择等)
   - 预计工作量: 1-2 天

3. **重新编译和测试**
   - 在 ARM64 Docker 中编译
   - 传输到设备
   - 测试物理显示

### 方案 A 示例代码结构

```cpp
// eink_refresh.h
class EInkRefresher {
public:
    static void init();
    static void refreshFullScreen();
    static void refreshPartial(QRect rect);
private:
    static int fb_fd;
};

// main.cpp 集成
class MyWebView : public QWebEngineView {
protected:
    void paintEvent(QPaintEvent *event) override {
        QWebEngineView::paintEvent(event);
        EInkRefresher::refreshPartial(event->rect());
    }
};
```

## 当前阶段总结

| 阶段 | 状态 | 说明 |
|------|------|------|
| A-E | ✅ 完成 | 构建、部署、库依赖 |
| C | ✅ 完成 | linuxfb 配置 |
| D-1 | ✅ 完成 | 识别 E-Ink 刷新问题 |
| D-2 | 🔄 进行中 | VNC 功能验证 |
| E | ⏳ 待开始 | E-Ink 刷新集成 |

---

**更新时间**: 2025-11-16
**VNC 服务状态**: ✅ 运行中 (端口 5900)
**下一步**: 连接 VNC 验证应用功能，然后决定 Phase E 的实施方案
