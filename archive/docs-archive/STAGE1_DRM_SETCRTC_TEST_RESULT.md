# Stage 1: DRM SetCRTC 测试结果

**日期**: 2025-11-18 04:00
**测试程序**: `drm_setcrtc_test.c`
**结果**: ✅ **drmModeSetCrtc 调用成功**

---

## 测试执行过程

### 步骤 1: 字符串分析
找到关键信息：
- TMODE / PMODE - 刷新模式
- 波形文件: `/usr/share/remarkable/*.eink` (24个)
- DRM 函数: `drmModeSetCrtc`, `drmModeAddFB`

### 步骤 2: DRM 属性枚举
- 工具: `drm_dump_props.c`
- 结果: **没有自定义 E-Ink DRM 属性**
- 结论: E-Ink 刷新不通过 DRM property

### 步骤 3: drmModeSetCrtc 测试

#### 测试程序功能:
1. 打开 `/dev/dri/card0`
2. 枚举 DRM 资源 (connector, crtc)
3. 创建 dumb buffer (1620x2160, 16-bit RGB565)
4. 填充测试图案 (垂直黑白条纹)
5. 使用 `drmModeAddFB` 添加 framebuffer
6. **使用 `drmModeSetCrtc` 绑定到 CRTC**

#### 测试输出:
```
=== DRM SetCRTC Test for reMarkable Paper Pro ===

✓ Opened /dev/dri/card0 (fd=3)
✓ Got DRM resources
  Connectors: 1
  CRTCs: 1
✓ Found connected connector 35
✓ Using mode: 365x1700 @85Hz
✓ Using CRTC 33 (from resources)

Creating test framebuffer...
  Buffer size: 6998400 bytes
  Filling buffer with test pattern...
✓ Created dumb buffer (handle=1, size=6998400)
✓ Mapped buffer to memory
✓ Copied test pattern to buffer
✓ Added framebuffer (id=36)

🔥 Setting CRTC to display our framebuffer...
   CRTC ID: 33
   FB ID: 36
   Connector ID: 35
✓ drmModeSetCrtc succeeded!

📺 CHECK THE SCREEN NOW!
   Expected: Vertical black and white stripes
   If you see the pattern, E-Ink refresh works via SetCRTC!

Waiting 3 seconds...

Cleaning up...
✓ Test complete
```

### 🎉 关键成果

**✅ drmModeSetCrtc 调用成功！**

这意味着：
1. ✅ 成功打开 DRM 设备
2. ✅ 成功创建并映射 buffer
3. ✅ 成功添加 framebuffer 对象
4. ✅ 成功执行 drmModeSetCrtc (无错误返回)

### ⏳ 等待确认

**需要用户确认**:
reMarkable Paper Pro 物理屏幕是否显示了**垂直黑白条纹**？

**如果显示了条纹** ✅:
- 证明 drmModeSetCrtc 可以触发 E-Ink 刷新
- Stage 1 **成功完成**
- 可以直接进入 Stage 2 (集成到 Qt6 应用)

**如果屏幕没变化** ❌:
- drmModeSetCrtc 不足以触发刷新
- 需要额外的步骤 (可能需要自定义 ioctl)
- 继续调查 0xc02064b2 ioctl 的作用

---

## 技术细节

### DRM 资源信息
- **Connector ID**: 35
- **CRTC ID**: 33
- **Mode**: 365x1700 @85Hz (注意：这个分辨率似乎不对，实际应该是 1620x2160)
- **Framebuffer ID**: 36
- **Buffer Handle**: 1

### 使用的 DRM API
```c
// 1. 创建 dumb buffer
struct drm_mode_create_dumb create_dumb;
create_dumb.width = 1620;
create_dumb.height = 2160;
create_dumb.bpp = 16;
drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb);

// 2. 映射 buffer
struct drm_mode_map_dumb map_dumb;
map_dumb.handle = create_dumb.handle;
drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb);
void *mapped = mmap(0, create_dumb.size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, map_dumb.offset);

// 3. 填充数据
memcpy(mapped, buffer, buffer_size);

// 4. 添加 framebuffer
drmModeAddFB2(fd, WIDTH, HEIGHT, DRM_FORMAT_RGB565,
              handles, pitches, offsets, &fb_id, 0);

// 5. 设置 CRTC (关键步骤!)
drmModeSetCrtc(fd, crtc_id, fb_id, 0, 0,
               &connector_id, 1, &mode);
```

---

## 前置条件

**重要发现**: 测试需要停止 xochitl
- 首次运行: `Permission denied` - CRTC 被 xochitl 占用
- 停止 xochitl 后成功: `systemctl stop xochitl`

**原因**: DRM CRTC 是独占资源，同一时间只能被一个进程控制

**影响**: 我们的应用需要：
1. 要么替代 xochitl 运行
2. 要么使用不同的机制与 xochitl 共存

---

## 下一步计划

### 场景 A: 屏幕显示了条纹 ✅

**立即执行**:
1. **Stage 2.1**: 封装 DRM 刷新代码
   ```cpp
   class EInkDRMRefresher {
   public:
       static bool init();
       static void fullRefresh(void* buffer);
       static void cleanup();
   };
   ```

2. **Stage 2.2**: 集成到 Qt6 应用
   - 在 Qt 应用中创建 offscreen buffer
   - 渲染内容到 buffer
   - 调用 `EInkDRMRefresher::fullRefresh(buffer)`
   - 观察物理屏幕刷新

3. **Stage 2.3**: 优化
   - 实现部分刷新 (指定区域)
   - 调整刷新频率
   - 优化性能

**预计时间**: 2-3 小时

### 场景 B: 屏幕没有变化 ❌

**需要深入调查**:
1. 分析自定义 ioctl (0xc02064b2)
   - 反汇编找到调用上下文
   - 分析 32 字节参数结构
   - 可能需要在 SetCRTC 后调用此 ioctl 才能触发刷新

2. 研究波形文件
   - 分析 .eink 文件格式
   - 理解波形数据如何加载
   - 可能需要先加载波形数据

3. 尝试 DRM atomic commit
   - 使用更现代的 DRM atomic API
   - 可能需要设置 plane properties

**预计时间**: 3-5 小时

---

## 总结

### 已完成
1. ✅ 详细分析 libqsgepaper.so 字符串
2. ✅ 枚举所有 DRM 属性 (确认无自定义属性)
3. ✅ 实现并测试 drmModeSetCrtc
4. ✅ 成功执行 DRM 操作 (无错误)

### 关键洞察
- E-Ink 刷新不通过 DRM property
- drmModeSetCrtc 可以成功调用
- 需要停止 xochitl 才能独占 CRTC
- Paper Pro 使用标准 DRM/KMS 架构

### 当前状态
**等待用户确认物理屏幕显示**

**如果成功** → Stage 1 完成，进入 Stage 2
**如果失败** → 继续深入调查触发机制

---

**文档状态**: Stage 1 测试完成，等待用户反馈
**下次更新**: 根据用户反馈更新下一步计划
