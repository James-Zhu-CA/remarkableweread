# Deployment & Environment (Key Information)

## Device Connection
- **IP Address**: `10.11.99.1`
- **Username**: `root`
- **Password**: `QpEXvfq2So`
- **Connection Method**: SSH / SCP
- **SSH Command Example**: `sshpass -p 'QpEXvfq2So' ssh root@10.11.99.1`

## Build Environment
- **System**: Docker (Ubuntu 22.04)
- **Architecture**: `aarch64` (ARM64)
- **Toolchain**: Cross-compilation toolchain for reMarkable
- **Build Path**: `/workspace/weread-test/build-cross`
- **Framework**: Qt 6.8.2 + WebEngine (Custom Build)

## Deployment Paths
- **App Directory**: `/home/root/weread/`
- **Executable**: `/home/root/weread/WereadEinkBrowser`
- **Logs**: `/home/root/weread/logs/`
- **System Service**: `xovi-tripletap.service` (for launcher interaction)
