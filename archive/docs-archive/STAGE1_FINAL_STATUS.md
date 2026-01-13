# Stage 1 最终状态报告

**日期**: 2025-11-18 05:00
**状态**: 🎯 **找到 E-Ink ioctl，验证存在，需确定参数结构**

---

## 核心成果

### ✅ 完全确认的发现

1. **E-Ink 刷新使用自定义 DRM ioctl**
   - ioctl 1: `0xc02064b2` (178, 32字节, READ|WRITE)
   - ioctl 2: `0xc01064b3` (179, 16字节, READ|WRITE)

2. **调用序列** (从 libqsgepaper.so 反汇编)
   ```c
   ioctl(fd, 0xc02064b2, &config);   // 配置刷新参数
   drmModeAddFB(...);                 // 注册 framebuffer
   ioctl(fd, 0xc01064b3, &refresh);  // 触发刷新
   ```

3. **ioctl 存在性验证** ✅
   - ioctl 1: 返回 `EINVAL` (Invalid argument) - **ioctl 存在但参数不对**
   - ioctl 2: 返回 `ENOENT` (No such file or directory) - 可能依赖 ioctl 1

4. **架构理解**
   - Paper Pro 使用 DRM/KMS (`/dev/dri/card0`)
   - **不使用** 传统 mxcfb (`/dev/fb0` 不存在)
   - xochitl 使用 Qt epaper 平台插件 + libqsgepaper.so
   - 第三方应用使用 qtfb-shim (通过 `/tmp/qtfb.sock` 与服务器通信)

---

## 当前障碍

### ⚠️ 参数结构未知

**问题**: 我们知道 ioctl 命令号，但不知道精确的参数结构。

**当前推测** (32字节配置, 16字节刷新):
```c
// 猜测 - 需要验证
struct eink_config_params {
    uint32_t x, y, width, height;  // 16 bytes
    uint32_t waveform;              // 4 bytes
    uint32_t flags;                 // 4 bytes
    uint32_t reserved[2];           // 8 bytes
};  // Total: 32 bytes

struct eink_refresh_params {
    uint32_t fb_id;       // 4 bytes
    uint32_t flags;       // 4 bytes
    uint32_t reserved[2]; // 8 bytes
};  // Total: 16 bytes
```

**测试结果**: 均返回错误，说明结构不对。

---

## 尝试的方法

### ✅ 成功的调查

1. **反汇编分析** - 找到 ioctl 命令号和调用序列
2. **字符串分析** - 识别关键组件 (TMODE, PMODE, waveform)
3. **DRM 属性枚举** - 确认无自定义 property
4. **测试程序验证** - 确认 ioctl 存在

### ❌ 失败的尝试

1. **drmModeSetCrtc** - 不足以触发刷新
2. **dlopen libqsgepaper.so** - ABI 不兼容导致 segfault
3. **strace xochitl** - strace 未安装/不可用
4. **推测参数结构** - EINVAL 错误

---

## 下一步选项

### 选项 A: 深入反汇编分析参数构造 ⭐ 推荐

**方法**: 详细分析 libqsgepaper.so 中 ioctl 调用前的参数准备代码

**步骤**:
1. 反汇编 0x58ae0 - 0x58b28 区域 (ioctl 1 准备)
2. 反汇编 0x58b50 - 0x58b94 区域 (ioctl 2 准备)
3. 追踪寄存器值，理解参数如何填充
4. 重建 C 结构体定义

**预计时间**: 2-3 小时

**成功率**: 高 (70-80%)

### 选项 B: 查找内核驱动源码

**方法**: 搜索 reMarkable Paper Pro 的 DRM 驱动源码

**可能位置**:
- reMarkable 官方 GitHub (如果有开源驱动)
- NXP i.MX93 参考驱动
- Linux kernel mainline (如果已合并)

**搜索关键字**:
- `DRM_IOCTL` + `0xb2` 或 `178`
- `i.MX93` + `E-Ink` + `DRM`
- `reMarkable` + `Chiappa` + `driver`

**预计时间**: 1-4 小时 (取决于是否公开)

**成功率**: 中等 (40-60%)

### 选项 C: 逆向工程 + 试错

**方法**: 基于已知信息系统尝试不同参数组合

**策略**:
1. 参考 mxcfb 结构 (已知的 reMarkable 2 格式)
2. 调整字段顺序和大小
3. 尝试不同的 enum 值
4. 使用 errno 反馈迭代

**预计时间**: 4-8 小时

**成功率**: 低-中等 (30-50%)

### 选项 D: 使用 qtfb-shim 方案 (绕过 ioctl)

**方法**: 不直接调用 ioctl，而是使用 qtfb-shim 架构

**架构**:
```
Qt6 应用 (使用 linuxfb)
  ↓ LD_PRELOAD
qtfb-shim.so (拦截 framebuffer 写入)
  ↓ Unix socket
qtfb 服务器 (已有实现，如 rm2fb)
  ↓ 正确的 ioctl
DRM 驱动
```

**优点**:
- 不需要知道精确的 ioctl 参数
- 使用已验证的组件
- KOReader 使用此方案

**缺点**:
- 需要运行额外的服务进程
- 依赖外部组件
- 性能可能不如直接调用

**预计时间**: 2-3 小时 (集成现有组件)

**成功率**: 高 (80-90%)

---

## 技术资源

### 已分析的关键文件

1. **libqsgepaper.so** (`/usr/lib/plugins/scenegraph/libqsgepaper.so`)
   - 包含完整的 E-Ink 刷新实现
   - 反汇编已保存: `/tmp/qsgepaper_disasm.txt`
   - 关键地址: 0x58b28 (ioctl 1), 0x58b94 (ioctl 2)

2. **qtfb-shim.so** (`/home/root/shims/qtfb-shim.so`)
   - LD_PRELOAD shim 拦截 framebuffer
   - 通过 `/tmp/qtfb.sock` 与服务器通信
   - 提供 sendPartialUpdate/sendCompleteUpdate API

3. **KOReader 代码**
   - 设备定义: `frontend/device/remarkable/device.lua`
   - 依赖 qtfb-shim 在 Paper Pro 上运行
   - Framebuffer: `ffi/framebuffer_mxcfb.lua`

### 波形文件

设备上的波形数据:
```
/usr/share/remarkable/*.eink (24 个文件)
例如: GAL3_AAB0B9_IC0D01_AC073MC1F2_AD1004-GCA_TC.eink
```

格式未知，但可能在 ioctl 参数中引用。

---

## 建议的行动计划

### 🎯 推荐路径：选项 A + D 组合

**阶段 1: 深入反汇编 (2-3 小时)**
1. 详细分析参数构造代码
2. 尝试重建结构体
3. 编写测试验证

**如果成功** ✅:
- 直接进入 Stage 2 (集成到 Qt6)
- 完全掌控刷新机制
- 最佳性能

**如果失败** ❌:
- 进入阶段 2

**阶段 2: 使用 qtfb-shim (2-3 小时)**
1. 研究 qtfb-shim 协议
2. 部署 qtfb 服务器 (rm2fb 或类似)
3. 配置 Qt6 应用使用 shim
4. 验证刷新功能

**总预计时间**: 4-6 小时

**整体成功率**: 90%+

---

## 代码资产

### 已创建的测试程序

1. **drm_dump_props.c** - DRM 属性枚举工具
   - 验证无自定义 E-Ink 属性

2. **drm_setcrtc_test.c** - SetCRTC 测试
   - 验证 SetCRTC 不足以触发刷新

3. **drm_eink_ioctl_test.c** - 自定义 ioctl 测试
   - 验证 ioctl 存在
   - 需要正确的参数结构

### 文档

- [STAGE1_MAJOR_FINDINGS.md](STAGE1_MAJOR_FINDINGS.md) - 主要发现总结
- [STAGE1_4B_DRM_ANALYSIS.md](STAGE1_4B_DRM_ANALYSIS.md) - DRM API 分析
- [STAGE1_TEST_RESULT_UPDATED.md](STAGE1_TEST_RESULT_UPDATED.md) - 测试结果

---

## 关键洞察

1. **Paper Pro 是 DRM-only 设备**
   - 完全基于 DRM/KMS
   - 无传统 framebuffer (`/dev/fb0`)
   - 需要 DRM atomic 或自定义 ioctl

2. **自定义 ioctl 是关键**
   - 标准 DRM API (SetCRTC) 不触发刷新
   - 必须使用自定义 ioctl 178/179
   - 参数结构是最后障碍

3. **多种可行路径**
   - 直接 ioctl (需要参数结构)
   - qtfb-shim (不需要知道参数)
   - 两者都可以工作

---

## 下一步决策

**问题**: 选择哪个路径？

**A 路径** (深入反汇编):
- 优点: 完全控制，最佳性能
- 缺点: 需要更多分析时间
- 推荐给: 想要最优解的项目

**D 路径** (qtfb-shim):
- 优点: 快速, 已验证, 可靠
- 缺点: 依赖外部组件
- 推荐给: 想要快速原型的项目

**建议**: 先尝试 A (2-3小时)，如果受阻则切换到 D。

---

**当前状态**: Stage 1 ~85% 完成
**待完成**: 确定参数结构或采用 qtfb-shim
**预计完成时间**: 4-6 小时
