# WeRead Browser 快速启动指南

## 一、连接设备

```bash
ssh root@10.11.99.1
# 密码: QpEXvfq2So
```

## 二、启动应用

### 方法 1: 使用启动脚本 (推荐)

```bash
cd /home/root/weread
./run-weread-browser.sh
```

### 方法 2: 手动启动

```bash
cd /home/root/weread

export LD_LIBRARY_PATH=/home/root/weread/qt6/lib
export QT_PLUGIN_PATH=/home/root/weread/qt6/plugins
export QML2_IMPORT_PATH=/home/root/weread/qt6/qml

# 显示配置 - 关键!
export QT_QPA_PLATFORM=linuxfb
export QT_QPA_FB_DRM=1
export QT_QPA_FB_HIDECURSOR=1

# WebEngine 配置
export QTWEBENGINE_DISABLE_SANDBOX=1
export QTWEBENGINE_CHROMIUM_FLAGS="--disable-gpu --no-sandbox --single-process"

# 启动
./apps/weread-browser/bin/weread-browser
```

## 三、检查运行状态

### 查看进程

```bash
ps | grep weread
```

**预期输出**:
```
1902 root     1435m S    {weread-browser} /home/root/weread/apps/weread-brows
1904 root      280m S    /home/root/weread/qt6/libexec/QtWebEngineProcess --t
```

### 查看日志

```bash
cat /tmp/weread-linuxfb.log
```

**正常日志示例**:
```
Detected locale "C" with character encoding "ANSI_X3.4-1968", which is not UTF-8.
Qt depends on a UTF-8 locale, and has switched to "C.UTF-8" instead.
Sandboxing disabled by user.
Unable to detect GPU vendor.
Spellchecking can not be enabled.
[3468:3468:1116/192830.114243:ERROR:system_network_context_manager.cpp(293)] Cannot use V8 Proxy resolver in single process mode.
```

以上警告均为**预期**,不是错误。

### 查看显示状态

```bash
cat /sys/class/drm/card0/card0-LVDS-1/status
cat /sys/class/drm/card0/card0-LVDS-1/modes
```

**预期输出**:
```
connected
365x1700
```

## 四、停止应用

```bash
killall -9 weread-browser QtWebEngineProcess
```

## 五、故障排查

### 问题 1: "error while loading shared libraries"

**症状**: 提示缺少 .so 文件

**解决**:
```bash
# 检查库路径
ls -lh /home/root/weread/qt6/lib/ | grep -i <缺失的库名>

# 如果文件存在,确认 LD_LIBRARY_PATH 设置正确
echo $LD_LIBRARY_PATH
```

### 问题 2: "Cannot create window: no screens available"

**症状**: 无法创建窗口

**解决**: 确认启用了 DRM 支持
```bash
export QT_QPA_FB_DRM=1  # 必须设置!
```

### 问题 3: 进程启动后立即退出

**症状**: `ps` 看不到进程

**解决**: 查看详细日志
```bash
# 前台运行查看完整输出
cd /home/root/weread
export LD_LIBRARY_PATH=/home/root/weread/qt6/lib
export QT_PLUGIN_PATH=/home/root/weread/qt6/plugins
export QT_QPA_PLATFORM=linuxfb
export QT_QPA_FB_DRM=1
export QTWEBENGINE_DISABLE_SANDBOX=1

./apps/weread-browser/bin/weread-browser 2>&1 | tee /tmp/debug.log
```

### 问题 4: 屏幕无显示

**可能原因**:
1. 页面正在加载 (等待 30-60 秒)
2. 显示被其他应用占用
3. DRM 配置问题

**调试步骤**:
```bash
# 运行最小测试程序
cd /home/root/weread
export LD_LIBRARY_PATH=/home/root/weread/qt6/lib
export QT_PLUGIN_PATH=/home/root/weread/qt6/plugins
export QT_QPA_PLATFORM=linuxfb
export QT_QPA_FB_DRM=1

./apps/minimal_test
```

应该看到:
```
=== Qt6 Minimal EGLFS Test ===
Qt Version: 6.8.2
Platform: linuxfb
Creating window...
Window shown, entering event loop...
Window exposed! Drawing...
```

如果 minimal_test 能显示,说明显示系统正常,weread-browser 可能只是加载慢。

## 六、性能优化建议

### 减少内存占用

编辑启动脚本,添加更多 Chromium 参数:

```bash
export QTWEBENGINE_CHROMIUM_FLAGS="\
--disable-gpu \
--disable-gpu-compositing \
--disable-smooth-scrolling \
--disable-accelerated-2d-canvas \
--disable-webgl \
--disable-accelerated-video-decode \
--no-sandbox \
--single-process \
--disable-dev-shm-usage \
--disable-software-rasterizer \
--disable-extensions \
--disable-plugins \
--disable-background-networking \
--disable-sync"
```

### 清理缓存

```bash
rm -rf /home/root/.cache/weread/
rm -rf /home/root/.local/share/weread/
```

## 七、预期行为

### 正常启动流程

1. **0-5 秒**: 进程启动,加载库
2. **5-15 秒**: Qt 初始化,创建窗口
3. **15-30 秒**: WebEngine 启动,加载网页
4. **30-60 秒**: 微信读书网站完全加载

### 内存占用

- 主进程: ~1.4 GB
- 渲染进程: ~280 MB × 2
- 总计: ~2 GB

⚠️ **注意**: 接近设备内存上限,如果出现内存不足,尝试:
```bash
# 清理系统缓存
sync
echo 3 > /proc/sys/vm/drop_caches
```

### 网络连接

应用会访问:
- https://weread.qq.com
- 微信读书 CDN 资源

确保设备有网络连接:
```bash
ping -c 3 weread.qq.com
```

## 八、已知限制

1. **性能**: 页面加载较慢 (CPU 性能限制)
2. **内存**: 接近设备上限,不建议多标签
3. **显示**: E-Ink 刷新较慢,动画效果差
4. **GPU**: 无硬件加速,纯软件渲染

## 九、下一步

1. **物理确认**: 查看 reMarkable 屏幕,确认有显示输出
2. **触摸测试**: 尝试触摸屏幕交互
3. **登录测试**: 扫码登录微信读书
4. **阅读测试**: 打开书籍,验证阅读体验

---

**更新时间**: 2025-11-16
**版本**: 1.0
