# Stage 1 - ioctl logger 捕获（2025-11-19 13:05 UTC-5）

## 环境
- 设备：reMarkable Paper Pro Move（chiappa）
- 连接：USB `10.11.99.1`，root/QpEXvfq2So
- 工具：`weread-test/ioctl_logger.c` + `weread-test/scripts/build-ioctl-logger.sh`（生成 `/tmp/ioctl_logger.so`）
- 日志：`docs-archive/logs/xochitl_ioctl_logger_20251119.log`

## 操作步骤
1. `sshpass` + `scp` 将 `/tmp/ioctl_logger.so` 复制到 `/home/root/`
2. `systemctl stop xochitl`
3. `LD_PRELOAD=/home/root/ioctl_logger.so /usr/bin/xochitl --system > /home/root/xochitl_ioctl.log`
4. 运行约 10 秒（设备界面出现多次刷新），随后 `kill <pid>` 停止
5. `systemctl start xochitl` 恢复默认 UI
6. 拷贝日志到本地 `/tmp/xochitl_ioctl.log` 并归档

## 关键发现
- `/dev/dri/card0` 正常，logger 绑定 fd=17（DRM 节点）
- 新 ioctl 编号：
  - `0xc02064b2`：配置（32 字节）——**目标参数已捕获**
  - `0xc01064b3`：刷新（16 字节）——**目标参数已捕获**
  - `0xc01c64ae`：配置后紧跟 28 字节 ioctl，可能是波形/队列
  - `0xc01064b5`、`0xc02064b6`、`0xc02064b9` 等额外命令需要继续分析
- `0xc02064b2` 数据恒定：`a4 06 00 00 6d 01 00 00 10 00 00 00 ...`  
  - 推测字段：`width=0x06a4 (1700)`、`height=0x016d (365)`、`x=0x10 (16)`，其余零填；需要更多操作以触发 Y/高度变化验证
- `0xc01064b3` 前 4 字节递增（1..16），猜测为刷新序号/marker，剩余字段为零

### 扩展捕获（2025-11-19 13:29 UTC-5）
- 更新 logger 后再次抓取，日志：`docs-archive/logs/xochitl_ioctl_logger_20251119_v2.log`
- 新增 hexdump：
  - `0xc01c64ae` (Mode params，28B)：每次与 config 相同的宽高/偏移值 + 末尾 4B 递增（0x01..0x10），疑似刷新队列槽 ID
  - `0xc01064b5` (Marker，16B)：出现 2 组 payload，包含时间戳样值 `0x06593d90` 等，可能为 `WAIT_FOR_COMPLETE`
  - `0xc02064b6` (Buffer params，32B)：含 `0x1f` 字段与偏移/stride（0x10），第二条出现非零 offset/handle
  - `0xc02064b9` (Eink params，32B)：第二条包含 0x30425906 等地址 + `0x10 0d 00 00`，推测波形/温度配置
  - `0xc03864bc` (56B)：大量重复 payload（468 次），结构含四个地址指针 + 尾部 `0x30`、疑似 `drmModeAddFB2` 或 buffer sync 数据
- 所有 config/refresh payload 仍与首次一致，说明默认 UI 触发为固定全屏刷新

## 待办
- ✅ 更新 logger，增加对 `0xc01c64ae / 0xc01064b5 / 0xc02064b6 / 0xc02064b9 / 0xc03864bc` 等命令的 hexdump
- [ ] 结合 `0xc01c64ae (28 bytes)` 递增字段推导波形/队列语义，并确认是否与 `0xc01064b3` 的 marker 对应
- [ ] 触发不同区域刷新或波形模式（局刷、INIT、A2 等），观察 32 字节 payload 的变化以锁定 `{x,y,width,height,waveform,flags}`
- [ ] 将结构假设回写到 `EInkRefresher` 或新 `DrmRefresher`

### 用户手写/触控捕获（2025-11-19 14:03 UTC-5）
- 操作：用户在屏幕上点击、书写数秒
- 日志：`docs-archive/logs/xochitl_ioctl_logger_20251119_draw.log`
- 结果：`config` / `mode` / `refresh` 仍固定在 1700×365、slot=1..16、marker=1..16；未观察到局部刷新参数变化
- 推论：xochitl 在默认主页/手写界面仍触发固定的条带式刷新，可能需要进入绘图应用或其他 UI 才会发起不同矩形

### 绘图应用捕获（2025-11-19 14:07 UTC-5）
- 操作：用户进入官方绘图/笔记应用，进行绘制、擦除等操作
- 日志：`docs-archive/logs/xochitl_ioctl_logger_20251119_drawing.log`
- 结果：参数与前两次一致，未见 `0xc02064b2` 或 `0xc01c64ae` 的矩形发生变化；`buffer/eink/marker` 同样保持两种固定 payload
- 推论：绘图应用也沿用固定的条带刷新，需寻找其他进程（例如 qtfb server/rm2fb demo）或直接实现测试程序才能逼出不同区域
