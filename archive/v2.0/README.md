# WeRead E-Ink Browser v2.0

**版本日期**: 2025-12-10 21:48:39

## 版本说明

这是 WeRead E-Ink Browser 的 2.0 版本代码快照，包含前后端完整源代码。

## 文件结构

```
v2.0/
├── README.md           # 本文件
├── frontend/           # 前端代码 (WereadEinkBrowser)
│   ├── main.cpp        # 主程序 (约 2,580 行)
│   ├── shm_writer.cpp  # 共享内存写入器
│   ├── shm_writer.h    # 共享内存写入器头文件
│   └── CMakeLists.txt  # 前端构建配置
└── backend/            # 后端代码 (epaper_shm_viewer)
    ├── main.cpp        # 后端主程序 (约 19K)
    ├── main.qml        # QML UI 定义
    └── CMakeLists.txt  # 后端构建配置
```

## 主要特性

### 前端 (WereadEinkBrowser)

1. **得到书籍页配置**:
   - 默认 UA: Kindle
   - 默认 Zoom: 2.0
   - 滚动翻页机制（无 wakeUp 闪烁）

2. **Ready 监控机制**:
   - 书籍页面: 检查特定 DOM 元素和文本内容
   - 非书籍页面: 仅检查 `document.body` 存在（条件较宽松）

3. **翻页抓帧**:
   - 固定 6 帧抓取（100/500/1000/1500/2000/3000ms）
   - 支持微信读书和得到书籍页

4. **Chromium 优化**:
   - 禁用 GPU 相关功能
   - 启用 HTTP 缓存（50MB）
   - 6 个渲染线程

### 后端 (epaper_shm_viewer)

1. **共享内存读取**: 从 `/dev/shm/weread_frame` 读取帧数据
2. **DRM 刷新控制**: 通过 DRM ioctl 控制 e-ink 刷新
3. **QML UI**: 使用 QML 渲染界面

## 已知问题

1. **非书籍页面 Ready 条件过宽松**: 
   - 当前仅检查 `document.body` 存在即判定 ready
   - 可能导致页面内容未完全加载就停止监控
   - 建议：增加文本内容检查（如 `bodyLen > 100`）

## 构建说明

### 前端构建

```bash
docker exec qt6-arm-builder bash -c "
  cd /workspace/weread-test && 
  rm -rf build-cross && mkdir build-cross && 
  /workspace/qt6-src/build-qt6/qtbase/bin/qt-cmake -G Ninja \
    -S /workspace/weread-test -B /workspace/weread-test/build-cross \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DCMAKE_PREFIX_PATH=/workspace/qt6-src/build-qt6/qtbase/lib/cmake && 
  cmake --build /workspace/weread-test/build-cross
"
```

### 后端构建

```bash
cd bridge-mvp/viewer && 
docker run --rm -v "$PWD":/workspace -w /workspace ubuntu:22.04 bash -lc "
  apt-get update >/dev/null && 
  apt-get install -y cmake ninja-build build-essential g++-aarch64-linux-gnu \
    libpcre2-16-0 libglib2.0-0:arm64 libxkbcommon0:arm64 libharfbuzz0b:arm64 libdbus-1-3:arm64 >/dev/null && 
  /workspace/qt6-src/build-qt6/qtbase/bin/qt-cmake -G Ninja \
    -S . -B build-cross \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DCMAKE_PREFIX_PATH=/workspace/qt6-src/build-qt6/qtbase/lib/cmake && 
  cmake --build build-cross
"
```

## 部署路径

- **前端**: `/home/root/weread/WereadEinkBrowser`
- **后端**: `/home/root/weread/epaper_shm_viewer`

## 代码关键位置

### 前端 (main.cpp)

- **得到书籍页配置** (第 1537-1552 行): `ensureDedaoDefaults()`
- **Ready 监控** (第 1739-1960 行): `pollReady()`
- **翻页抓帧** (第 1611-1639 行): `schedulePageTurnCaptures()`
- **得到滚动翻页** (第 1151-1275 行): `dedaoScroll()`
- **UA 切换** (第 1513-1533 行): `updateUserAgentForUrl()`

### 后端 (main.cpp)

- **共享内存读取**: 从 `/dev/shm/weread_frame` 读取
- **DRM 刷新**: 通过 DRM ioctl 控制 e-ink

## 版本历史

- **v2.0** (2025-12-10): 
  - 得到书籍页 UA 改为 Kindle
  - Zoom 调整为 2.0
  - 去除滚动后的 wakeUp 闪烁操作
  - 非书籍页面 ready 条件较宽松（待优化）

