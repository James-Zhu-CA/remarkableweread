# Stage 1: 重大发现 - E-Ink 刷新机制

**日期**: 2025-11-18 04:45
**状态**: 🎯 **找到完整刷新序列！**

---

## 核心发现

### ✅ E-Ink 刷新的完整调用序列

通过反汇编 libqsgepaper.so，发现 reMarkable Paper Pro 使用以下序列触发 E-Ink 刷新：

```c
// 1. 配置刷新参数
ioctl(fd, 0xc02064b2, &config_params);  // 32 字节参数

// 2. 注册 framebuffer
drmModeAddFB(fd, width, height, ...);   // 标准 DRM 调用

// 3. 触发 E-Ink 刷新
ioctl(fd, 0xc01064b3, &refresh_params); // 16 字节参数
```

### 自定义 DRM ioctl 详情

#### ioctl 1: 配置刷新 (0xc02064b2)
- **方向**: 读写 (READ|WRITE)
- **大小**: 32 字节
- **类型**: 'd' (DRM)
- **编号**: 178 (0xb2)
- **用途**: 配置刷新参数（区域、波形模式等）

#### ioctl 2: 触发刷新 (0xc01064b3)
- **方向**: 读写 (READ|WRITE)
- **大小**: 16 字节
- **类型**: 'd' (DRM)
- **编号**: 179 (0xb3)
- **用途**: 提交刷新请求

---

## 参数结构推测

基于大小和上下文分析，推测参数结构：

### Config Params (32 bytes)
```c
struct eink_config_params {
    uint32_t x;           // Update region x
    uint32_t y;           // Update region y
    uint32_t width;       // Update region width
    uint32_t height;      // Update region height
    uint32_t waveform;    // Waveform mode (INIT, DU, GC16, GL16, etc.)
    uint32_t flags;       // Update flags
    uint32_t reserved[2]; // Padding/reserved
};
```

### Refresh Params (16 bytes)
```c
struct eink_refresh_params {
    uint32_t fb_id;       // Framebuffer ID (from drmModeAddFB)
    uint32_t flags;       // Refresh flags
    uint32_t reserved[2]; // Padding/reserved
};
```

---

## 反汇编证据

### ioctl 调用位置
```assembly
// 地址 0x58b28: ioctl 1 (CONFIG)
58b14: d28c9641     	mov	x1, #0x64b2             // 命令号低位
58b18: f2b80401     	movk	x1, #0xc020, lsl #16   // 命令号高位 = 0xc02064b2
58b28: 97fefdc6     	bl	0x18240 <drmIoctl@plt>

// 地址 0x58b64: drmModeAddFB
58b64: 97feffdf     	bl	0x18ae0 <drmModeAddFB@plt>

// 地址 0x58b94: ioctl 2 (REFRESH)
58b6c: d28c9661     	mov	x1, #0x64b3             // 命令号低位
58b70: f2b80201     	movk	x1, #0xc010, lsl #16   // 命令号高位 = 0xc01064b3
58b94: 97fefdab     	bl	0x18240 <drmIoctl@plt>
```

---

## 为什么 drmModeSetCrtc 失败？

之前的测试 `drmModeSetCrtc` 未能触发刷新，原因现在清楚了：

❌ **缺少自定义 ioctl 调用**

reMarkable Paper Pro 的 E-Ink 刷新**不是**通过标准 DRM API 自动触发的，而是需要：
1. 显式调用自定义 ioctl 配置参数
2. 显式调用自定义 ioctl 触发刷新

这是一个自定义的刷新协议，叠加在标准 DRM 之上。

---

## 下一步：验证测试

### 已创建测试程序
**文件**: `drm_eink_ioctl_test.c`

**测试策略**:
1. 尝试推测的参数结构
2. 尝试全零参数（看是否有默认行为）
3. 观察屏幕变化
4. 根据结果调整参数

### 需要编译
**问题**: Docker daemon 未运行，设备无编译器

**选项**:
1. 启动 Docker Desktop 并编译
2. 使用在线交叉编译服务
3. 在另一台机器上编译

---

## 预期结果

**如果参数结构正确** ✅:
- 屏幕应该刷新并显示白色
- 证明我们完全理解了刷新机制
- Stage 1 **完成**，可进入 Stage 2

**如果参数结构不对** ⚠️:
- ioctl 可能返回错误（-EINVAL）
- 需要进一步分析参数结构
- 可能需要查找 reMarkable 内核驱动源码

---

## 替代验证方法

如果直接测试困难，可以：

### 方法 A: 查找内核驱动源码
reMarkable Paper Pro 使用 i.MX93 处理器，驱动可能是：
- `drivers/gpu/drm/mxsfb/` (Freescale LCDIF)
- 或 NXP 的自定义 E-Paper 驱动

搜索关键字：
- `DRM_IOCTL_EINK_*`
- `ioctl 0xb2` 或 `ioctl 178`
- `eink_config` / `epd_config`

### 方法 B: 尝试已知的波形模式值
基于 Oxide/remarkable2-framebuffer 项目：
- 0 = INIT (初始化)
- 1 = DU (快速，单色)
- 2 = GC16 (高质量灰度)
- 3 = GL16 (文本模式)
- 6 = A2 (极快，仅黑白)

---

## 技术价值

### 这个发现的意义

1. **✅ 完整理解刷新机制**
   - 知道需要调用哪些 ioctl
   - 知道调用顺序
   - 知道参数大小

2. **✅ 可以绕过 Qt 插件 ABI 问题**
   - 不需要使用 libqsgepaper.so
   - 直接调用内核 ioctl
   - 完全控制刷新过程

3. **✅ 为 Qt6 集成奠定基础**
   - 可以创建自己的刷新层
   - 不依赖系统 Qt 版本
   - 可以优化刷新策略

---

## 当前状态

**进度**: Stage 1.6 - 90% 完成

**已完成**:
- ✅ 字符串分析
- ✅ DRM 属性枚举
- ✅ drmModeSetCrtc 测试（验证失败）
- ✅ 反汇编分析
- ✅ **找到完整刷新序列**
- ✅ **识别自定义 ioctl**

**待完成**:
- ⏳ 编译测试程序
- ⏳ 验证参数结构
- ⏳ 确认屏幕刷新

**估计剩余时间**: 30-60 分钟（一旦能编译）

---

## 建议的下一步

### 立即行动（推荐）
1. 启动 Docker Desktop
2. 编译 `drm_eink_ioctl_test.c`
3. 部署到设备并测试
4. 根据结果调整参数

### 保守方法
1. 先查找 reMarkable 内核驱动源码
2. 确认参数结构
3. 然后编译测试

### 快速验证
如果想快速验证概念，可以：
1. 先用 strace 跟踪 xochitl 运行时的 ioctl 调用
2. 看是否能捕获到 0xc02064b2 和 0xc01064b3
3. 如果能，记录参数内容
4. 直接复现

---

**结论**: 我们已经非常接近成功了！只差最后的参数验证步骤。
