# Appload Persistence & Troubleshooting Postmortem

## 问题背景
用户在重启 reMarkable Paper Pro 后，Appload 侧边栏消失。重新安装和手动启动尝试均未能持久化解决问题，且遇到 `hashtab` 重建失败和 `LD_PRELOAD` 不生效的情况。

## 根本原因分析 (Root Causes)

1.  **文件系统持久性 (Filesystem Persistence)**:
    *   **现象**: 在 `/etc/systemd/system/xochitl.service.d/` 下创建的配置文件在重启后消失。
    *   **原因**: reMarkable 的系统更新或启动机制可能会重置 `/etc` 下的特定目录，或者 `xovi`原本的启动脚本使用了不稳定的 `tmpfs` 挂载，导致配置无法落地。
    *   **教训**: 对于需要跟随系统服务启动的配置，直接写入只读分区 `/usr/lib/systemd/system/` (需 remount rw) 比依赖 `/etc` 或 `tmpfs` 更为稳健和持久。

2.  **动态库加载 (Shared Library Loading)**:
    *   **现象**: 即使设置了 `LD_PRELOAD=/home/root/xovi/xovi.so`，`xochitl` 进程内存中也没有加载该库，且日志报错 "cannot open shared object file"。
    *   **原因**: `systemd` 服务环境可能对 `/home` 目录有访问限制（如 Sandboxing 或 Mount Namespace 隔离），或者 `LD_PRELOAD` 在跨分区加载时存在隐式限制。
    *   **教训**: 将关键系统注入库移动到标准的系统库目录 `/usr/lib/libxovi.so` 可以规避绝大多数路径权限和加载问题。

3.  **进程冲突 (Process Conflicts)**:
    *   **现象**: `rebuild_hashtable` 脚本显示 "Saved" 但文件实际上未生成，或者生成后立即消失。
    *   **原因**: 若系统中存在残留的 `xochitl` 进程（即便是僵尸进程或正在退出的进程），运行重建工具会导致 Framebuffer 锁竞争 (`Failed to lock epframebuffer`)，导致进程在写入文件前或写入中途崩溃。
    *   **教训**: 在执行涉及 Framebuffer 或系统核心文件的维护操作前，必须使用 `killall -9` 确保环境绝对干净，不能仅依赖 `systemctl stop`。

## 最佳实践与防范措施

### 1. 部署策略
*   **优先使用系统路径**: 核心组件部署到 `/usr/lib` 和 `/usr/bin`。
*   **避免 ephemeral 路径**: 不要依赖 `tmpfs` 或易被重置的路径做持久化。

### 2. 脚本编写
*   **强制清理**: 启动或维护脚本开头应包含激进的进程清理逻辑。
    ```bash
    killall -9 xochitl || true
    while pidof xochitl; do sleep 1; done
    ```
*   **验证逻辑**: 脚本不应只检查退出码，必须检查**文件产物**是否存在。
    ```bash
    if [ ! -f "$TARGET_FILE" ]; then exit 1; fi
    ```

### 3. 调试手段
*   **内存映射检查**: 验证注入是否成功的唯一真理是查看内存映射，而不是看环境变量。
    ```bash
    grep libname /proc/$(pidof process)/maps
    ```
*   **全链路日志**: 关注 `journalctl` 中 `ld.so` (加载器) 的报错，它通常被忽视但能直接指出路径或架构问题。
