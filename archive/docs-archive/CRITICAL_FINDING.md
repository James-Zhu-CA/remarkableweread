# 关键发现：Paper Pro 不使用 mxcfb ioctl

**日期**: 2025-11-18 02:00
**测试**: Stage 1.3 E-Ink 刷新测试

---

## 🔍 测试结果

### 测试程序
- ✅ 成功编译 (ARM64)
- ✅ 成功传输到设备
- ✅ 成功打开 `/dev/dri/card0`
- ❌ **ioctl 失败: "Inappropriate ioctl for device"**

### 详细输出
```
=== E-Ink Refresh Test for reMarkable Paper Pro ===

Trying to open: /dev/fb0
  Failed: No such file or directory
Trying to open: /dev/dri/card0
✓ Successfully opened: /dev/dri/card0 (fd=3)

Sending E-Ink refresh command...
  Region: 1620x2160 at (0,0)
  Waveform: GL16 (mode 3)
  Update mode: FULL (0x1)
  Marker: 1

✗ ioctl(MXCFB_SEND_UPDATE) failed: Inappropriate ioctl for device
```

---

## 💡 结论

### ❌ 排除的方案
**传统 mxcfb ioctl (reMarkable 2 的方案) 不适用于 Paper Pro**

证据：
1. ✅ `/dev/fb0` 不存在 - Paper Pro 没有传统 framebuffer
2. ✅ `/dev/dri/card0` 可以打开 - Paper Pro 使用 DRM/KMS
3. ❌ `MXCFB_SEND_UPDATE` ioctl 返回 "Inappropriate ioctl for device"

### ✅ Paper Pro 架构差异

| 特性 | reMarkable 2 | reMarkable Paper Pro |
|------|--------------|----------------------|
| 显示接口 | Framebuffer (`/dev/fb0`) | **DRM/KMS only** (`/dev/dri/card0`) |
| E-Ink ioctl | `MXCFB_SEND_UPDATE` | **不支持 mxcfb ioctl** |
| 刷新机制 | 传统 framebuffer + ioctl | **DRM API / 自定义机制** |
| EPFramebuffer | EPFramebufferSwtcon | **EPFramebufferAcep2** (不同实现) |

---

## 🎯 下一步方案

### 方案 1: 使用 libqsgepaper.so (推荐)

**思路**: 直接链接系统的 E-Paper 库

**原因**:
- ✅ libqsgepaper.so 肯定知道如何刷新 Paper Pro
- ✅ 它包含 EPFramebufferAcep2 的实现
- ✅ 避免重复造轮子

**挑战**:
- ⚠️ 缺少头文件
- ⚠️ 需要逆向推导 API
- ⚠️ 可能有 ABI 兼容性问题

**实施**:
1. 复制 libqsgepaper.so 到我们的应用目录
2. 使用 `dlopen()` 动态加载
3. 使用 `dlsym()` 查找刷新函数
4. 直接调用

示例代码：
```cpp
// 动态加载 libqsgepaper.so
void *handle = dlopen("/usr/lib/libqsgepaper.so", RTLD_NOW);

// 查找符号 (例如 EPFramebuffer::sendUpdate)
typedef void (*RefreshFunc)(int, int, int, int);
RefreshFunc refresh = (RefreshFunc)dlsym(handle, "_ZN13EPFramebuffer10sendUpdateEiiii");

// 调用刷新
refresh(0, 0, 1620, 2160);
```

---

### 方案 2: 使用标准 DRM API

**思路**: Paper Pro 可能使用标准 DRM atomic commit

**原因**:
- ✅ Paper Pro 使用现代 DRM/KMS
- ✅ 标准 DRM API 有完整文档
- ✅ libdrm 库支持

**步骤**:
1. 使用 `drmModeSetCrtc` 或 `drmModeAtomicCommit`
2. 可能有自定义 DRM property 用于触发 E-Ink 刷新
3. 查看 libqsgepaper.so 使用的 DRM API

**参考**:
```c
#include <xf86drm.h>
#include <xf86drmMode.h>

int fd = open("/dev/dri/card0", O_RDWR);
drmModeRes *resources = drmModeGetResources(fd);

// 枚举 properties
for (int i = 0; i < connector->count_props; i++) {
    drmModePropertyPtr prop = drmModeGetProperty(fd, connector->props[i]);
    printf("Property: %s\n", prop->name);
    // 查找类似 "EINK_REFRESH" 的 property
}
```

---

### 方案 3: 反汇编 libqsgepaper.so 找到刷新逻辑

**思路**: 从库中直接提取刷新代码

**步骤**:
1. 反汇编 `libqsgepaper.so`
2. 查找 `EPFramebuffer::swapBuffers` 实现
3. 分析使用的系统调用
4. 复现相同逻辑

**命令**:
```bash
objdump -d /tmp/libqsgepaper.so > disasm.txt
# 查找 swapBuffers 函数
grep -A100 "swapBuffers" disasm.txt
```

---

## 📝 总结

**Stage 1 状态**: ⚠️ **部分成功 + 重大发现**

**已完成**:
1. ✅ 找到了 Oxide 的 mxcfb.h 头文件
2. ✅ 成功编写并编译测试程序
3. ✅ 确认 Paper Pro **不使用** 传统 mxcfb ioctl
4. ✅ 明确了架构差异

**关键洞察**:
> reMarkable Paper Pro 使用了完全不同的 E-Ink 刷新机制，不兼容 reMarkable 2 的 mxcfb ioctl。我们需要：
> 1. 使用系统库 (libqsgepaper.so)，或
> 2. 理解 DRM 刷新机制，或
> 3. 反向工程 libqsgepaper.so

**推荐方案**: 方案 1 - 动态加载 libqsgepaper.so

**预计时间**:
- 方案 1: 2-3 小时
- 方案 2: 4-6 小时 (需要研究)
- 方案 3: 6-8 小时 (需要汇编知识)

**下一步**: 尝试方案 1 - 使用 dlopen 加载 libqsgepaper.so

---

**重要**: 这个发现改变了我们的策略，但并不是坏事！说明 Paper Pro 使用更现代的架构，我们有多种途径可以解决。
