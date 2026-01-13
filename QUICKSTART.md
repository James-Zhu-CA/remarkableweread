# WeRead E-Ink Browser - 快速开始

> **reMarkable Paper Pro Move** 微信读书浏览器 | Qt6 WebEngine | 电子墨水屏优化

---

## 📦 快速安装

### 前置要求

- **设备**: reMarkable Paper Pro Move (ARM64, 954×1696)
- **网络**: 设备与开发机在同一局域网
- **SSH访问**: 已启用SSH并获取root密码

### 一键安装

```bash
# 1. 克隆项目
git clone https://github.com/YOUR_USERNAME/remarkableweread.git
cd remarkableweread

# 2. 设置设备IP（替换为你的设备IP）
export DEVICE_IP=10.11.99.1
export DEVICE_PASS=YOUR_PASSWORD

# 3. 部署应用
./scripts/quick-deploy.sh $DEVICE_IP $DEVICE_PASS
```

安装完成后，在设备上打开"微信读书"图标即可使用。

---

## 🚀 开发者安装（从源码编译）

### 1. 环境准备

#### 1.1 安装Docker（推荐）

```bash
# macOS
brew install docker

# Linux
sudo apt-get install docker.io

# 启动Docker
docker pull ghcr.io/YOUR_USERNAME/qt6-arm-builder:latest
```

#### 1.2 或使用本地工具链

```bash
# 下载reMarkable交叉编译工具链
wget https://remarkable.engineering/oecore-x86_64-cortexa53-toolchain-3.1.15.sh
chmod +x oecore-x86_64-cortexa53-toolchain-3.1.15.sh
sudo ./oecore-x86_64-cortexa53-toolchain-3.1.15.sh

# 激活工具链环境
source /opt/oecore*/environment-setup-cortexa53-remarkable-linux
```

### 2. 编译应用

```bash
# 使用Docker编译（推荐）
docker exec qt6-arm-builder bash -c "cd /workspace/src && cmake --build build-cross"

# 或使用本地工具链
cd src
mkdir -p build-cross && cd build-cross
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/remarkable-toolchain.cmake ..
cmake --build .
```

### 3. 部署到设备

```bash
# 方法1: 使用部署脚本（推荐）
./scripts/deploy.sh 10.11.99.1 YOUR_PASSWORD

# 方法2: 手动部署
sshpass -p 'YOUR_PASSWORD' scp src/build-cross/WereadEinkBrowser root@10.11.99.1:/tmp/WereadEinkBrowser.new
sshpass -p 'YOUR_PASSWORD' ssh root@10.11.99.1 "cp /tmp/WereadEinkBrowser.new /home/root/weread/WereadEinkBrowser"

# 验证部署成功
sshpass -p 'YOUR_PASSWORD' ssh root@10.11.99.1 "stat -c '%Y' /home/root/weread/WereadEinkBrowser"
```

### 4. 启动应用

```bash
# SSH到设备
ssh root@10.11.99.1

# 停止系统UI（释放屏幕）
systemctl stop xochitl

# 启动应用
cd /home/root/weread
./WereadEinkBrowser

# 恢复系统UI（退出应用后）
systemctl start xochitl
```

---

## 📱 使用说明

### 基本操作

- **打开应用**: 点击设备上的"微信读书"图标
- **翻页**: 
  - 手写笔点击屏幕左/右侧
  - 物理按键
- **缩放**: 双指捏合/展开
- **刷新**: 三指下滑
- **退出**: 长按电源键 → 选择"关闭应用"

### 菜单功能

点击屏幕顶部呼出菜单：

1. **微信读书/得到** - 切换服务
2. **目录** - 打开章节目录
3. **字体 +** - 增大字体
4. **字体 -** - 缩小字体

### 性能说明

- **启动时间**: 约3-5秒
- **内存占用**: 120-180MB
- **字体调整**: 约30-35秒（需重新加载页面）

---

## 🔧 故障排除

### 应用无法启动

```bash
# 检查进程
ssh root@10.11.99.1 "ps | grep WereadEinkBrowser"

# 查看日志
ssh root@10.11.99.1 "journalctl -u weread-browser -n 50"

# 检查文件权限
ssh root@10.11.99.1 "ls -lh /home/root/weread/WereadEinkBrowser"
```

### 屏幕显示异常

```bash
# 重启系统UI
ssh root@10.11.99.1 "systemctl restart xochitl"

# 清理缓存
ssh root@10.11.99.1 "rm -rf /home/root/.cache/weread/*"
```

### 部署失败

```bash
# 确认设备可达
ping 10.11.99.1

# 确认SSH连接
ssh root@10.11.99.1 "echo 'Connection OK'"

# 检查磁盘空间
ssh root@10.11.99.1 "df -h /home"
```

### 字体调整慢

这是正常现象。微信读书是React SPA应用，字体调整需要重新加载整个页面（约30-35秒）。这是架构限制，无法优化。

---

## 📚 更多文档

- **完整README**: [README.md](README.md) - 详细技术文档
- **开发规则**: [AGENTS.md](AGENTS.md) - Agent工作规则
- **经验教训**: [docs/lessons_learned.md](docs/lessons_learned.md) - 开发经验总结
- **项目状态**: [PROJECT_STATUS.md](PROJECT_STATUS.md) - 当前进度

---

## 🤝 贡献指南

欢迎提交Issue和Pull Request！

### 开发流程

1. Fork项目
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启Pull Request

### 代码规范

- 遵循[AGENTS.md](AGENTS.md)中的规则
- 所有代码必须通过编译
- 提交前运行`./scripts/lint.sh`

---

## 📄 许可证

MIT License - 详见 [LICENSE](LICENSE)

---

## 🙏 致谢

- [Oxide Browser](https://github.com/Eeems/oxide) - Qt WebEngine参考实现
- [reMarkable Community](https://remarkablewiki.com/) - 开发资源和支持
- Qt Project - 优秀的跨平台框架
