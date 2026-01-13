# Stage 1.1 进展报告

**日期**: 2025-11-18 01:40
**任务**: 追踪 xochitl 的 ioctl 调用

---

## 📊 执行情况

### ✅ 已完成
1. ✅ strace 工具已部署到设备 (`/tmp/strace`)
2. ✅ 确认 xochitl 使用 FD 17 访问 `/dev/dri/card0`
3. ✅ 尝试多种 strace 追踪方式

### ❌ 遇到的问题

**问题**: strace 未能捕获到 DRM/E-Ink 相关的 ioctl 调用

**原因分析**:
1. **界面静止**: xochitl 在追踪期间可能没有刷新屏幕
2. **线程问题**: 刷新可能在特定线程中，追踪参数可能不完整
3. **可能使用其他机制**: Paper Pro 可能使用 DRM atomic commit 或其他高级 API

**尝试过的方法**:
```bash
# 方法 1: 追踪运行中的进程
strace -f -e trace=ioctl -p <pid>
# 结果: 只捕获到 FS_IOC_GETFSLABEL (文件系统 ioctl)

# 方法 2: 追踪启动过程
strace -f -e trace=ioctl systemctl start xochitl
# 结果: 未能捕获到 xochitl 本身的 ioctl

# 方法 3: 详细追踪所有线程
strace -f -e trace=ioctl -v -s 1000 -p <pid>
# 结果: 仍然只有文件系统 ioctl
```

---

## 💡 替代方案

### 方案 1: 基于已知信息直接实现 ⭐ 推荐

**思路**: 基于 Oxide 项目的 EPFramebuffer 实现，直接编写刷新代码

**依据**:
1. ✅ 我们已经找到了 Oxide 的 E-Ink 刷新实现 (`oxide/shared/epaper/epframebuffer.h`)
2. ✅ Oxide 是开源的，代码可以直接参考
3. ✅ reMarkable 2 和 Paper Pro 底层 E-Ink 机制可能相似

**执行步骤**:

1. **查看 Oxide 源码**:
   ```bash
   cd oxide/shared/epaper
   # 查找 ioctl 调用
   grep -r "ioctl\|MXCFB" *.cpp *.h
   ```

2. **提取 ioctl 定义**:
   - 查找 `MXCFB_SEND_UPDATE` 等宏定义
   - 查找刷新数据结构
   - 了解刷新参数

3. **适配到 Paper Pro**:
   - 使用 `/dev/dri/card0` 而不是 `/dev/fb0`
   - 调整可能不同的参数

4. **编写测试程序** (Stage 1.3):
   ```c
   // 基于 Oxide 的实现
   int fd = open("/dev/dri/card0", O_RDWR);
   // ... ioctl 调用
   ```

**优点**:
- ✅ 不依赖 strace
- ✅ 有成熟代码参考
- ✅ 可以快速验证

**风险**:
- ⚠️ Paper Pro 可能使用不同的 ioctl (但概率较低)

---

### 方案 2: 社区资源查找

**搜索关键词**:
- "reMarkable Paper Pro E-Ink ioctl"
- "reMarkable Paper Pro framebuffer"
- "i.MX93 E-Ink display"
- "EPFramebufferAcep2"

**可能的资源**:
- reMarkable 开发者论坛
- GitHub 上的 reMarkable 项目
- Oxide 项目的最新更新

---

### 方案 3: 反汇编 libqsgepaper.so (高级)

**思路**: 直接分析编译后的库，找到 ioctl 调用

**工具**:
```bash
# 反汇编查找 ioctl 调用
objdump -d libqsgepaper.so | grep -B20 -A5 "ioctl@plt"

# 查找立即数加载 (ioctl 命令号)
# ARM64 架构通常用 mov/movz 指令加载立即数
objdump -d libqsgepaper.so | grep -B10 "ioctl@plt" | grep "mov"
```

**优点**:
- ✅ 直接从官方库获取信息

**缺点**:
- ❌ 需要汇编知识
- ❌ 费时费力

---

## 🎯 建议下一步

### 立即执行: 方案 1 - 基于 Oxide 实现

**步骤 1**: 查看 Oxide E-Paper 源码 (10分钟)
```bash
cd /Users/jameszhu/AI_Projects/remarkableweread/oxide/shared/epaper
ls -la
```

**步骤 2**: 提取 ioctl 调用和数据结构 (20分钟)
- 找到 `MXCFB_SEND_UPDATE` 定义
- 找到 `mxcfb_update_data` 结构体
- 理解刷新参数含义

**步骤 3**: 编写最小测试程序 (30分钟)
```c
// eink_test.c
#include <fcntl.h>
#include <sys/ioctl.h>

// 从 Oxide 提取的定义
#define MXCFB_SEND_UPDATE  ...
struct mxcfb_update_data {
    // ...
};

int main() {
    int fd = open("/dev/dri/card0", O_RDWR);
    // ... 调用 ioctl
}
```

**步骤 4**: 在设备上测试 (10分钟)
```bash
gcc -o eink_test eink_test.c
./eink_test
# 观察屏幕是否有反应
```

---

## 📝 总结

**Stage 1.1 状态**: ⚠️ 部分完成 - strace 方法未成功，转向替代方案

**下一步**:
1. 查看 Oxide 源码
2. 提取 E-Ink 刷新 ioctl
3. 编写测试程序

**预计时间**: 1-2 小时完成 Stage 1

**成功概率**: 🟢 高 (80%+) - Oxide 已经证明这个方法可行
