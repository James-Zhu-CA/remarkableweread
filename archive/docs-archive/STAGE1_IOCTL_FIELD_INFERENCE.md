# Stage 1 - ioctl 参数拆解（基于 2025-11-19 捕获）

## 数据来源
- Logger：`weread-test/ioctl_logger.c`（2025-11-19 扩展版）
- 日志：`docs-archive/logs/xochitl_ioctl_logger_20251119.log` 与 `_v2.log`
- 触发方式：`LD_PRELOAD=/home/root/ioctl_logger.so /usr/bin/xochitl --system`（默认 UI 多次刷新）

## 结构推测

| ioctl | 字节数 | 小端拆解（`<I`） | 备注 |
|-------|--------|--------------------|------|
| `0xc02064b2` (Config) | 32 | `[1700, 365, 16, 0, 0, 0, 0, 0]` | 依次为 `width`, `height`, `x`, `y`, `???`，其余字段始终为 0（默认全屏局刷区域为 1700x365，x=16） |
| `0xc01c64ae` (Mode/Queue) | 28 | `[0, 365, 1700, 730, 16, 16, N]` | 第二、三、四项与 config 相同（但高度/宽度互换，疑似 `{y, height, width, ??}` 排列）；最后一个 DWORD 递增 1..16，实践中固定对应 `0xc01064b3` 的 marker |
| `0xc01064b3` (Refresh) | 16 | `[marker, 0, 0, 0]` | `marker` 递增 1..16，对应每次刷新队列；其余字段为 0 |
| `0xc01064b5` (Marker wait) | 16 | `[0x06593d90, 0, 1, 0]` 或 `[0, 0, 0, 0]` | 第一条含高地址（推测为 `drm_wait_vblank` 结果或时间戳），第二条清零；可能用于同步 |
| `0xc02064b6` (Buffer config) | 32 | `[0x1f, 0, 0, off?, 0x10, 0, 0x7, 0x065d58150]` | 字段 0 为固定 0x1f（命令 ID?），字段 4 = 0x10（stride?），字段 6/7 给出帧缓冲 handle/offset（第二个样本非零） |
| `0xc02064b9` (Eink param) | 32 | `[0, 0, 0, 0, 0, 0x1f, 0xEEEEEEEE, 0]` 或 `[0x06594230, 0, 0x06594270, 0, 0xD, 0x1F, 0xEEEEEEEE, 0]` | 第二条出现两个地址及 `0xD`（可能是 waveform/temperature 编码），`0x1F` 与 buffer 命令一致 |
| `0xc03864bc` (Unknown) | 56 | `[0, 1, ptrA, 0xffff, ptrB, 0xffff, ptrC, 0xffff, ptrD, 0xffff, 0, 0, 0, 0]` × 468 | 4 个连续指针 + 长度/flag，疑似 `drmModeAddFB2` 参数，后续可与 buffer 结构对照 |

> 说明：所有地址类值均来自默认 UI，尚未验证在局部刷新或不同波形下的演变。

## 尚存疑问
1. `0xc01c64ae` 字段顺序：推测 `[reserved, y, width?, height?, x1, x2, slot]`；默认 UI/书写动作仍输出固定 1700×365 条带，需要更激进的局部刷新场景来验证。
2. `0xc02064b6 / 0xc02064b9` 共同使用 `0x1f`：可能为刷新队列 ID 或波形模式 ID，需要进一步操作（例如触发 INIT/A2）观察数值是否变化。
3. `0xc03864bc` 重复频繁，推测为缓冲区同步或 `drmModeAtomicCommit` 的 plane state；需要交叉参考 DRM 文档或内核驱动符号。

## 下一步建议
1. **局部刷新实验**：在设备上运行可控测试程序（或在 xochitl 中打开/拖动窗口）以触发更小矩形，比较 `0xc02064b2` 与 `0xc01c64ae` 的前 3-4 个 DWORD。
2. **波形/模式实验**：通过系统设置或 qtfb server 调用不同 waveform（A2/DU/GL16），观察 `0xc02064b9`、`0xc01c64ae` 的变化。
3. **同步机制验证**：检查 `0xc01064b5` 与 `0xc01064b3` 的调用顺序，确认是否为 “提交 – 等待完成” 对，便于在自研刷新器中实现 `waitForComplete`.
4. **结构验证**：在 Qt 浏览器中实现试验性 `DrmRefresher`，填充上述字段并尝试最小矩形刷新，以验证 ioctl 参数有效性。
