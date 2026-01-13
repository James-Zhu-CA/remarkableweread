# Stage 1.4b: DRM API 分析 - 详细记录

**日期**: 2025-11-18 03:30
**方法**: 分析 libqsgepaper.so 的 DRM 调用模式

---

## Step 1: 字符串分析 ✅ 完成

### libqsgepaper.so 关键字符串

**模式相关**:
- `TMODE` - T-Mode (某种刷新模式)
- `PMODE` - P-Mode
- `EPScreenMode` - 屏幕模式枚举
- `GhostControlMode` - 残影控制模式
- `LastScreenMode`

**波形相关**:
- `get_waveform_data`
- `Loading waveforms from: %s`
- `wf_search: unable to find correct waveform for lot: %s and tft: %s`
- `.eink` - 波形文件扩展名

**设备文件**:
- `/tmp/epd.lock` - EPD 锁文件
- `devconfig serial_number_epd`

**DRM 函数调用**:
- `drmModeSetCrtc` ⭐ 关键函数
- `drmModeAddFB` ⭐ 关键函数
- `drmModeGetProperty`
- `drmModeObjectGetProperties`
- `drmIoctl` ⭐ 直接 ioctl 调用

**波形文件位置** (设备上):
```
/usr/share/remarkable/*.eink (24个文件)
例如: GAL3_AAB0B9_IC0D01_AC073MC1F2_AD1004-GCA_TC.eink
```

---

## Step 2: DRM 属性枚举 ✅ 完成

### 工具: drm_dump_props.c

**DRM 资源枚举结果**:
```
Connectors: 1
CRTCs: 1
Encoders: 1
Planes: 0  ⚠️ 异常 - 通常应该有 plane
```

### 发现的 DRM 属性

**Connector 35 属性**:
1. `EDID` = 0
2. `DPMS` = 3 (Off) - 电源管理
   - 0: On
   - 1: Standby
   - 2: Suspend
   - 3: Off
3. `link-status` = 0 (Good)
4. `non-desktop` = 0
5. `TILE` = 0

**CRTC 33 属性**:
1. `VRR_ENABLED` = 0 - Variable Refresh Rate

### 🔴 关键发现: **没有 E-Ink 相关的自定义 DRM 属性**

**缺失的预期属性**:
- ❌ EINK_*
- ❌ WAVEFORM_*
- ❌ UPDATE_MODE_*
- ❌ TMODE_*
- ❌ PMODE_*
- ❌ REFRESH_*

**结论**: E-Ink 刷新**不是**通过 DRM property 机制控制的

---

## Step 3: ioctl 调用分析

### 从反汇编中发现的 ioctl 调用

**地址 0x58b28**:
```assembly
58b14: d28c9641     	mov	x1, #0x64b2             // =25778
58b18: f2b80401     	movk	x1, #0xc020, lsl #16
58b28: 97fefdc6     	bl	0x18240 <drmIoctl@plt>
```

**ioctl 命令号**: `0xc02064b2`

**解析**:
```
0xc02064b2 拆解:
- 0xc0 = 方向位 (读+写)
- 0x20 = 32字节
- 0x64 = 'd' (DRM 类型字符)
- 0xb2 = 178 (命令号)
```

这是一个 DRM ioctl，32字节参数，读写操作。

---

## 推断: E-Ink 刷新机制

### 假设 1: 通过 drmModeSetCrtc 触发刷新

**理论**:
1. 应用绘制内容到内存 buffer
2. 调用 `drmModeAddFB(buffer)` 创建 framebuffer ID
3. 调用 `drmModeSetCrtc(crtc_id, fb_id)` 绑定 FB 到 CRTC
4. **DRM 驱动内部自动触发 E-Ink 刷新**

**证据**:
- libqsgepaper.so 大量使用 `drmModeSetCrtc`
- 字符串: `"unblanking: drmModeSetCrtc failed %d (%s)"`
- 这是标准 DRM 显示流程

**优点**:
- ✅ 标准 DRM API，有文档
- ✅ 不需要自定义 ioctl
- ✅ 驱动负责 E-Ink 时序

**挑战**:
- ⚠️ 无法精确控制刷新模式 (TMODE/PMODE)
- ⚠️ 无法指定波形
- ⚠️ 可能总是全屏刷新

### 假设 2: 自定义 ioctl 控制刷新参数

**理论**:
- `drmModeSetCrtc` 只是设置 framebuffer
- 刷新参数 (mode, waveform, region) 通过自定义 ioctl 设置
- ioctl `0xc02064b2` 可能就是这个命令

**需要验证**:
- 这个 ioctl 的参数结构是什么？
- 何时调用？(在 SetCrtc 之前还是之后)

---

## Next Step: 实验验证

### 实验 1: 最小 drmModeSetCrtc 测试

**目标**: 验证 `drmModeSetCrtc` 是否能触发屏幕刷新

**步骤**:
1. 打开 `/dev/dri/card0`
2. 分配一块内存作为 framebuffer (填充测试图案)
3. `drmModeAddFB` 创建 FB ID
4. `drmModeSetCrtc` 绑定到 CRTC
5. 观察屏幕是否有变化

**预期**:
- 最好情况: 屏幕刷新，显示测试图案 ✓
- 一般情况: 屏幕闪一下但内容不变 ~
- 最差情况: 完全没反应 ✗

### 实验 2: 枚举 framebuffer 对象属性

可能 E-Ink 属性不在 connector/crtc/plane 上，而在 framebuffer 对象本身？

**验证方法**:
```c
uint32_t fb_id = drmModeAddFB(...);
drmModeObjectProperties *props = drmModeObjectGetProperties(fd, fb_id, DRM_MODE_OBJECT_FB);
// 查看是否有 EINK 相关属性
```

### 实验 3: 分析自定义 ioctl (0xc02064b2)

**方法**:
1. 反汇编找到调用此 ioctl 的上下文
2. 分析传入的参数结构 (32字节)
3. 尝试复现调用

---

## 当前状态

**已验证**:
- ✅ libqsgepaper.so 使用 libdrm
- ✅ 主要函数: drmModeSetCrtc, drmModeAddFB
- ✅ 有自定义 ioctl: 0xc02064b2
- ✅ 波形文件存在: /usr/share/remarkable/*.eink
- ✅ 没有自定义 DRM property

**待验证**:
- ⏳ drmModeSetCrtc 是否触发刷新？
- ⏳ 自定义 ioctl 的作用？
- ⏳ 波形数据如何加载和使用？

**下一步行动**:
编写 `drm_setcrtc_test.c` - 测试最基本的 framebuffer 切换

---

## 记录时间线

- **03:00** - 发现 dlopen 方法失败 (SIGSEGV)
- **03:10** - 字符串分析，找到 TMODE/PMODE/waveform 线索
- **03:20** - 编写并运行 drm_dump_props.c
- **03:30** - 发现没有自定义 DRM property，转向分析 SetCrtc 机制
- **03:35** - 准备实验 1: drmModeSetCrtc 测试

**累计时间**: Stage 1.4b 已用 ~40 分钟

**预计剩余时间**:
- 实验 1: 30-45 分钟
- 如成功 → 进入 Stage 2 (集成)
- 如失败 → 实验 2/3 (再 1-2 小时)
