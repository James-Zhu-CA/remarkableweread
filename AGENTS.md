# WeRead E-Ink Browser - AGENTS

交流请以中文进行。

## 项目概述
Qt6 WebEngine 微信读书浏览器，目标设备 reMarkable Paper Pro Move (ARM64, 954×1696)

## 架构规则 (Non-negotiable)
- **单一职责**: 一个模块只有一个改变的理由
- **依赖方向**: 业务逻辑不依赖 UI/网络/框架代码
- **模块边界**: 跨模块交互必须通过小而明确的接口

## 代码拆分阈值 (Hard Guardrails)
| 指标 | 阈值 | 行动 |
|------|------|------|
| 单文件行数 | >800 | 必须拆分（生成代码/纯数据除外）|
| 单函数行数 | >60 | 必须重构 |
| 文件混合 UI+IO+业务逻辑 | 任何 | 必须拆分 |

## 目录结构
```
src/
├── app/          # 主程序入口、协调层
│   └── main.cpp  # 主文件（目标: <800行，当前需拆分）
├── ui/           # 视图/Widgets（纯展示）
├── domain/       # 业务规则（纯逻辑，无IO）
└── infra/        # IO实现（网络/数据库/文件系统）
```

## 构建 & 部署

### 快速命令
```bash
# 编译（使用现有 Docker 容器）
docker exec qt6-arm-builder bash -c "cd /workspace/src && cmake --build build-cross"

# 部署到运行中的设备（正确方式）
sshpass -p 'QpEXvfq2So' scp src/build-cross/WereadEinkBrowser root@10.11.99.1:/tmp/WereadEinkBrowser.new
sshpass -p 'QpEXvfq2So' ssh root@10.11.99.1 "cp /tmp/WereadEinkBrowser.new /home/root/weread/WereadEinkBrowser"
# 验证部署成功
sshpass -p 'QpEXvfq2So' ssh root@10.11.99.1 "stat -c '%Y' /home/root/weread/WereadEinkBrowser"
```

### 设备信息
- **IP**: `10.11.99.1` | **User**: `root` | **Pass**: `QpEXvfq2So`
- **应用目录**: `/home/root/weread/`
- **日志**: `/home/root/weread/logs/`

## Agent 工作规则

### 代码修改流程（必须遵守）

1.  **分析 → 方案 → 确认 → 执行**
    -   深入分析问题根本原因
    -   提出解决方案（含优缺点对比）
    -   **等待用户确认**
    -   确认后才开始修改代码

2.  **修改后验证**
    -   所有改动必须编译通过
    -   所有改动必须 lint/format 通过
    -   公共行为变更需更新文档

3.  **部署后验证**
    -   无需自动启动应用
    -   由用户手动启动并验证
    -   检查时间戳确认文件已替换

### 性能优化规则

尝试任何性能优化前，必须：
-   ✅ 测量各阶段实际耗时（不要靠猜）
-   ✅ 识别真正的瓶颈
-   ✅ 评估瓶颈是否可优化（第三方框架限制通常无法绕过）
-   ❌ 不要盲目实施复杂方案

**接受现实约束**：
-   某些等待时间是架构的合理代价
-   简单可靠 > 复杂炫技

### 功能删除检查清单

删除任何功能时，按顺序执行：
1.  grep搜索所有相关符号（变量名、函数名）
2.  删除UI创建代码
3.  删除成员变量
4.  删除函数实现
5.  删除函数声明（.h文件）
6.  删除所有调用点
7.  编译验证
8.  再次grep确认无残留

**结合原有规则**：保留验证过的最小闭环，避免删除关键步骤引发隐性回退

### 复杂方案验证规则

实施复杂方案前，必须：
1.  **先写POC验证核心假设**（不要假设，要验证）
2.  评估内存/性能开销是否可接受
3.  考虑后续维护成本
4.  假设不成立 → 立即停止

### 自动化交互规则（UI/Web）

-   **先确认可用状态**：确保目标已渲染且可见；不可见/无尺寸视为未就绪
-   **有限重试**：对"未就绪"类失败使用短延迟、有限次数重试，避免卡死
-   **成功信号验证**：用系统级结果（导航变化/状态更新）判定成功，不只看日志

## 关键配置

### Chromium 参数 (main.cpp)
```cpp
"--disable-gpu --disable-gpu-compositing --no-sandbox "
"--touch-events=enabled --disk-cache-size=52428800"
```

### 环境变量
| 变量 | 用途 |
|------|------|
| `WEREAD_LOG_LEVEL` | 日志级别 (error/warning/info) |
| `WEREAD_PING_DISABLE=1` | 禁用 ping 功能 |

---
**架构**: ARM64 (aarch64) | **框架**: Qt 6.8 + WebEngine | **工具链**: GCC 11.x

---

## 经验教训索引

完整案例参见：[`docs/lessons_learned.md`](docs/lessons_learned.md)

最新教训：
  - 性能瓶颈分析方法
  - 双View方案为何失败
  - 功能删除完整性检查
  - 部署流程改进
