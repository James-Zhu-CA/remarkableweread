# 下一步行动总结

**当前时间**: 2025-11-18 06:00
**当前状态**: Stage 1 完成 90%，准备进入实施阶段

---

## 当前情况

### ✅ 已确认的发现

1. **E-Ink 刷新机制**: 自定义 DRM ioctl (0xc02064b2, 0xc01064b3)
2. **ioctl 存在**: 已验证，返回 EINVAL（参数结构不对）
3. **设备架构**: DRM-only, 无传统 framebuffer
4. **xochitl**: 使用 Qt epaper 插件 + libqsgepaper.so
5. **第三方应用**: 需要 qtfb-shim（但设备上无 qtfb server）

### ⚠️ 当前障碍

1. **qtfb server 缺失**: `/tmp/qtfb.sock` 不存在
2. **Qt6 ABI 不兼容**: 无法直接使用系统 epaper 插件
3. **参数结构未知**: ioctl 可用但参数不对

---

## 三种可行路径

### 路径 A: 部署 qtfb server ⭐ 推荐快速验证

**优点**: 使用已验证的组件，快速跑通
**缺点**: 依赖外部服务

**步骤**:
1. 获取 rm2fb 或 rmpp-qtfb-shim server binary
2. 部署到设备并启动
3. 使用 qtfb-shim 运行应用
4. 验证显示和刷新

**预计时间**: 2-3 小时
**成功率**: 85%

### 路径 B: 使用 ioctl logger 反推参数 ⭐⭐ 推荐深入理解

**优点**: 获得完整控制权，性能最优
**缺点**: 需要额外分析时间

**步骤**:
1. 编译 ioctl_logger.so（已有代码）
2. Hook xochitl 进程
3. 触发屏幕刷新，记录参数
4. 从 hexdump 重建结构体
5. 实现直接 ioctl 调用

**预计时间**: 3-4 小时
**成功率**: 70%

### 路径 C: 集成到现有 weread-browser 应用 ⭐⭐⭐ 推荐最终方案

**优点**: 直接达成目标，完整解决方案
**缺点**: 需要修改应用代码

**步骤**:
1. 在 weread-browser 代码中添加刷新模块
2. 使用路径 A 或 B 的刷新机制
3. 集成到 WebView 的渲染流程
4. 添加刷新策略（快速/完整）

**预计时间**: 4-6 小时
**成功率**: 90%

---

## 建议的执行顺序

### 方案 1: 稳妥路线 (总计 6-9 小时)

```
Day 1:
  路径 A (2-3h) → 验证 qtfb-shim 可用
    ↓
  路径 C (4-6h) → 集成到 weread-browser
    ↓
  完成功能性应用

可选:
  路径 B (3-4h) → 性能优化
```

### 方案 2: 激进路线 (总计 3-4 小时)

```
Day 1:
  路径 B (3-4h) → 直接反推 ioctl 参数
    ↓ 如果成功
  集成到应用 (2h)
    ↓ 如果失败
  回退到路径 A
```

---

## 立即可执行的选项

### 选项 1: 编译 ioctl logger

**命令**:
```bash
# 已有完整代码，在 STAGE1_REVISED_STRATEGY.md

# 编译
docker exec qt6-arm-builder bash -c '
  gcc -shared -fPIC -o /tmp/ioctl_logger.so \
    -xc - -ldl << "EOF"
[ioctl_logger.c 代码]
EOF
'

# 部署
docker cp qt6-arm-builder:/tmp/ioctl_logger.so /tmp/
sshpass -p 'QpEXvfq2So' scp -o StrictHostKeyChecking=no \
  /tmp/ioctl_logger.so root@10.11.99.1:/home/root/

# 使用
LD_PRELOAD=/home/root/ioctl_logger.so /usr/bin/xochitl --system \
  2>&1 | tee /tmp/ioctl.log
```

### 选项 2: 查找并部署 qtfb server

**资源**:
- rm2fb: https://github.com/ddvk/remarkable2-framebuffer
- rmpp-qtfb-shim: https://github.com/asivery/rmpp-qtfb-shim

**步骤**:
1. 下载预编译 binary 或源码
2. 交叉编译（如需要）
3. 部署到设备
4. 启动服务

### 选项 3: 直接修改 weread-browser

**位置**: `/Users/jameszhu/AI_Projects/remarkableweread/qt6-src/`

**修改点**:
1. 添加 `EInkRefresher` 类
2. 在 WebView 渲染后调用刷新
3. 先使用 dummy 实现测试架构

---

## 决策点

**问题**: 您希望采用哪个路径？

**A. 稳妥快速** - 先跑通 qtfb-shim，再优化
- 开始: 获取 qtfb server
- 预期: 今天能看到工作的应用

**B. 深入理解** - ioctl logger 反推参数
- 开始: 编译 ioctl_logger.so
- 预期: 完全掌控刷新机制

**C. 直接集成** - 修改 weread-browser 应用
- 开始: 设计刷新接口
- 预期: 完整的应用解决方案

**D. 混合方案** - B + C 或 A + C
- 最全面但耗时最多

---

## 技术准备清单

### 已完成 ✅
- [x] DRM 属性分析
- [x] 反汇编 libqsgepaper.so
- [x] 识别自定义 ioctl
- [x] 验证 ioctl 存在
- [x] KOReader 代码分析
- [x] qtfb-shim 机制理解
- [x] 测试程序编写（drm_dump_props, drm_setcrtc_test, drm_eink_ioctl_test）

### 待完成 ⏳
- [ ] 选择执行路径
- [ ] 获取/编译必要组件
- [ ] 验证刷新功能
- [ ] 集成到应用
- [ ] 性能优化和策略调整

---

## 资源链接

**文档**:
- [主要发现](STAGE1_MAJOR_FINDINGS.md)
- [修订策略](STAGE1_REVISED_STRATEGY.md)
- [最终状态](STAGE1_FINAL_STATUS.md)

**代码**:
- 测试程序: `weread-test/drm_*.c`
- ioctl logger: [STAGE1_REVISED_STRATEGY.md](STAGE1_REVISED_STRATEGY.md#任务-31-编写-ioctl-logger)
- qtfb-shim 脚本: `weread-test/run-with-qtfb-shim.sh`

**外部资源**:
- rm2fb: https://github.com/ddvk/remarkable2-framebuffer
- rmpp-qtfb-shim: https://github.com/asivery/rmpp-qtfb-shim
- KOReader: `weread-test/archived/koreader/`

---

**下一步**: 请选择路径（A/B/C/D），我将立即开始执行。
