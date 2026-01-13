# 显示问题诊断报告

**日期**: 2025-11-16
**设备**: reMarkable Paper Pro Move (chiappa)
**问题**: Qt 应用运行正常但 E-Ink 屏幕无显示

---

## 已验证的事实

### ✅ 应用功能正常

1. **Qt6 + WebEngine 成功运行**
   - minimal_test: 正常启动，窗口创建成功
   - weread-browser: VNC 模式下完全正常
   - 无崩溃，无关键错误

2. **Qt 成功访问 DRM 设备**
   ```bash
   fuser /dev/dri/card0
   # 输出: 3441 (minimal_test)
   ```
   - Qt linuxfb 插件正常工作
   - DRM 设备正常打开和使用

3. **VNC 后端完全正常**
   ```bash
   QVncServer created on port 5900
   ```
   - 可通过 VNC 查看应用输出
   - 渲染、交互均正常

### ❌ E-Ink 物理显示问题

**症状**: 设备屏幕无任何变化

**发现**:
1. **DRM 显示状态**: `enabled = disabled`
2. **DPMS 电源状态**: `Off`
3. **xochitl 正常工作**: 重启后界面正常显示

---

## 根本原因分析

### 核心问题：E-Ink 需要主动刷新

reMarkable 使用 E-Ink 显示技术，与传统 LCD 不同：

| 传统 LCD | E-Ink (reMarkable) |
|----------|-------------------|
| 写入 framebuffer 即显示 | 写入 framebuffer + 刷新命令 |
| 实时更新 | 需要触发刷新波形 |
| Qt linuxfb 直接支持 | **需要额外驱动层** |

### Qt linuxfb 的局限

**标准 Qt linuxfb**:
- ✅ 成功打开 /dev/dri/card0
- ✅ 写入 DRM framebuffer
- ❌ **不知道如何触发 E-Ink 刷新**

**xochitl 的优势**:
- 使用专门的 E-Ink 驱动
- 调用 ioctl 触发刷新
- 管理刷新波形 (DU, GC16, GL16 等)

### 技术细节

**E-Ink 刷新机制** (基于 Oxide 项目分析):
```cpp
// 伪代码 - xochitl 的工作方式
int fb_fd = open("/dev/fb0", O_RDWR);  // 或 DRM 设备
// ... 绘制到 framebuffer ...
struct mxcfb_update_data update;
update.waveform_mode = WAVEFORM_MODE_GC16;  // 高质量灰阶
update.update_mode = UPDATE_MODE_FULL;      // 全屏刷新
update.update_region = {0, 0, 1620, 2160};
ioctl(fb_fd, MXCFB_SEND_UPDATE, &update);   // 触发刷新
```

**Qt linuxfb 实际行为**:
```cpp
// Qt 的行为
int drm_fd = open("/dev/dri/card0", O_RDWR);
// ... 写入 DRM planes ...
// 没有 ioctl 刷新调用 ❌
```

---

## 验证结论

### 显示管线状态

| 组件 | 状态 | 说明 |
|------|------|------|
| 应用代码 | ✅ 正常 | Qt6 API 无问题 |
| Qt linuxfb | ✅ 正常 | DRM 访问成功 |
| DRM 驱动 | ✅ 正常 | 设备可打开 |
| Framebuffer | ✅ 写入 | Qt 已写入数据 |
| **E-Ink 刷新** | ❌ **缺失** | 无刷新命令 |
| 物理显示 | ❌ 无变化 | 刷新未触发 |

### 问题定位

**不是**:
- ❌ Qt 编译问题
- ❌ 库依赖问题
- ❌ DRM 访问问题
- ❌ 应用逻辑问题

**是**:
- ✅ **E-Ink 刷新机制缺失**
- ✅ **需要集成 E-Ink 驱动层**

---

## 解决方案对比

### 方案 A: 集成 E-Ink 刷新代码 (推荐)

**描述**: 在应用中添加 E-Ink 刷新支持

**实施**:
```cpp
// 添加 eink_refresh.cpp
class EInkRefresher {
public:
    static void init(const QString& device = "/dev/dri/card0");
    static void refreshFull();  // 全屏刷新
    static void refreshPartial(QRect rect);  // 部分刷新
private:
    static int fb_fd;
    static void sendUpdate(int waveform, int mode, QRect rect);
};

// 在 QWebEngineView 中集成
class EInkWebView : public QWebEngineView {
protected:
    void paintEvent(QPaintEvent *event) override {
        QWebEngineView::paintEvent(event);
        // 每次绘制后触发刷新
        EInkRefresher::refreshPartial(event->rect());
    }
};
```

**优点**:
- ✅ 完全控制刷新策略
- ✅ 可优化性能（选择刷新模式）
- ✅ 独立于系统服务
- ✅ 工作量小 (2-4 小时)

**缺点**:
- ⚠️ 需要找到正确的 ioctl 命令 (MXCFB_SEND_UPDATE)
- ⚠️ 需要理解 reMarkable 的刷新协议

**工作量**: 2-4 小时
- 1 小时: 研究 ioctl 接口
- 1 小时: 实现刷新代码
- 1 小时: 集成到应用
- 1 小时: 测试和优化

### 方案 B: 重新编译 Qt6 with EGLFS/KMS

**描述**: 重新编译 Qt6，启用完整的 EGLFS KMS 支持

**实施**:
```bash
# 在 ARM64 Docker 中
cmake -G Ninja ../qt6-src \
  -DCMAKE_BUILD_TYPE=Release \
  -DFEATURE_webengine=ON \
  -DFEATURE_eglfs=ON \           # 启用 EGLFS
  -DFEATURE_eglfs_kms=ON \       # 启用 KMS 后端
  -DFEATURE_eglfs_gbm=ON \       # 启用 GBM
  -DFEATURE_linuxfb=ON
```

**优点**:
- ✅ 官方支持的方案
- ✅ 更完整的显示集成
- ✅ 可能自动处理刷新

**缺点**:
- ❌ 编译时间长 (3-6 小时)
- ❌ 可能仍需要 E-Ink 刷新代码
- ❌ **不确定 EGLFS 是否会触发 E-Ink 刷新** (可能性低)
- ❌ 增加复杂度

**工作量**: 4-8 小时
- 3-6 小时: 重新编译 Qt6
- 1 小时: 重新打包部署
- 1 小时: 测试

**关键不确定性**: EGLFS 也可能不会自动触发 E-Ink 刷新，最终仍需方案 A

### 方案 C: 使用 xochitl 作为显示服务器

**描述**: 让 xochitl 在后台运行，Qt 应用作为覆盖层

**优点**:
- ✅ 利用 xochitl 的 E-Ink 管理
- ✅ 无需修改应用代码

**缺点**:
- ❌ 资源占用高 (xochitl + Qt)
- ❌ 显示冲突可能性大
- ❌ 复杂的进程管理
- ❌ **已验证：xochitl 独占显示**

**结论**: **不可行**

### 方案 D: VNC 远程显示 (临时方案)

**描述**: 通过 VNC 使用应用，物理屏幕显示其他内容

**优点**:
- ✅ 已验证可用
- ✅ 功能完整
- ✅ 无需修改代码

**缺点**:
- ❌ 不满足"设备上显示"的需求
- ❌ 需要网络连接
- ❌ 不是长期方案

**结论**: **仅作为功能验证使用**

---

## 推荐方案

### 短期 (立即): 方案 D (VNC)

**目的**: 验证应用功能完整性

**步骤**:
1. 通过 VNC 连接到设备
2. 测试微信读书登录
3. 验证阅读功能
4. 确认无其他问题

**命令**:
```bash
# macOS 快速连接
open vnc://10.11.99.1:5900
```

### 中期 (接下来): 方案 A (E-Ink 刷新)

**目的**: 实现物理显示

**步骤**:
1. **研究 reMarkable ioctl** (1 小时)
   - 查看 Oxide 项目源码
   - 找到 MXCFB_SEND_UPDATE 定义
   - 理解刷新参数结构

2. **实现刷新代码** (1 小时)
   ```cpp
   // eink_refresh.cpp
   #include <linux/fb.h>
   #include <sys/ioctl.h>

   // 从 reMarkable 内核头文件获取
   #define MXCFB_SEND_UPDATE _IOW('F', 0x2E, struct mxcfb_update_data)

   void EInkRefresher::refreshFull() {
       struct mxcfb_update_data update = {0};
       update.waveform_mode = 2;  // GC16 高质量
       update.update_mode = 1;     // 全屏
       update.update_region = {0, 0, 1620, 2160};
       ioctl(fb_fd, MXCFB_SEND_UPDATE, &update);
   }
   ```

3. **集成到应用** (1 小时)
   - 修改 main.cpp
   - 在 QWebEngineView 中调用刷新
   - 测试不同刷新策略

4. **编译和部署** (30 分钟)
   - 在 ARM64 Docker 中编译
   - 传输到设备
   - 停止 xochitl 测试

5. **优化** (1 小时)
   - 调整刷新频率
   - 选择合适波形
   - 平衡性能和显示质量

**预期结果**: 物理屏幕成功显示

### 长期 (可选): 方案 A + 性能优化

**目的**: 提升用户体验

**优化方向**:
- 智能刷新策略 (静态内容用 partial, 翻页用 full)
- 波形优化 (文本用 GL16, 图片用 GC16)
- 刷新合并 (减少刷新次数)
- 添加刷新节流 (避免过度刷新)

---

## 不推荐方案 B 的原因

虽然 EGLFS 听起来更"正统"，但：

1. **不确定性高**: EGLFS 也是标准 DRM/KMS，不太可能自动处理 E-Ink 特殊刷新
2. **时间成本高**: 3-6 小时编译，可能最终仍需要方案 A
3. **风险大**: 可能引入新问题 (OpenGL 依赖、EGL 配置等)
4. **回报低**: 即使成功，仍缺少 E-Ink 优化

**结论**: 先尝试方案 A，如果失败再考虑方案 B

---

## 行动建议

### 立即行动 (5 分钟)

1. **通过 VNC 验证功能**
   ```bash
   open vnc://10.11.99.1:5900
   ```
   - 确认微信读书加载正常
   - 测试登录流程
   - 验证无其他bug

### 短期行动 (今天/明天)

2. **实施方案 A**
   - 研究 reMarkable ioctl 接口
   - 实现 E-Ink 刷新代码
   - 集成到应用并测试

### 如果方案 A 遇到困难

3. **备选：尝试方案 B**
   - 重新编译 Qt6 with EGLFS
   - 但预期仍需方案 A 的刷新代码

---

## 技术资源

### 参考代码

1. **Oxide 项目 EPFramebuffer**
   - 文件: `/Users/jameszhu/AI_Projects/remarkableweread/oxide/shared/epaper/epframebuffer.h`
   - 包含完整的 E-Ink 刷新实现 (Qt5)
   - 需要适配到 Qt6

2. **reMarkable 内核头文件**
   - 可能位置: `/usr/include/linux/mxcfb.h` (设备上)
   - 包含 ioctl 定义

3. **ddvk/remarkable-hacks**
   - GitHub: https://github.com/ddvk/remarkable-hacks
   - 包含 framebuffer 操作示例

### 关键数据结构 (预期)

```cpp
struct mxcfb_update_data {
    struct mxcfb_rect update_region;
    __u32 waveform_mode;
    __u32 update_mode;
    __u32 update_marker;
    int temp;
    unsigned int flags;
};

enum {
    WAVEFORM_MODE_INIT = 0,
    WAVEFORM_MODE_DU = 1,      // 快速黑白
    WAVEFORM_MODE_GC16 = 2,    // 高质量16级灰阶
    WAVEFORM_MODE_GL16 = 3,    // 文本优化
    // ...
};
```

---

**结论**: 核心问题已明确，解决方案清晰，建议立即验证 VNC 功能，然后实施方案 A。
