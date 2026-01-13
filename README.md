# WeRead E-Ink Browser

> 在reMarkable Paper Pro上享受微信读书的完整体验

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/Platform-reMarkable-blue.svg)](https://remarkable.com/)
[![Qt](https://img.shields.io/badge/Qt-6.8-green.svg)](https://www.qt.io/)

---

## 🎯 项目简介

**WeRead E-Ink Browser** 是专为reMarkable Paper Pro设备打造的微信读书浏览器。基于Qt6 WebEngine，针对电子墨水屏深度优化，让你在护眼的E-Ink屏幕上享受完整的微信读书体验。

### ✨ 核心优势

- **📚 完整功能** - 支持微信读书所有功能：登录、购买、阅读、笔记
- **👁️ 护眼体验** - 电子墨水屏，长时间阅读不伤眼
- **✍️ 自然交互** - 手写笔和触摸双重支持，翻页、标注流畅自然
- **🚀 性能优化** - 3-5秒启动，120-180MB内存占用
- **🎨 简洁界面** - 4个核心按钮，专注阅读体验

### 📸 效果展示

```
┌─────────────────────────────────┐
│  微信读书                    ☰  │  ← 简洁菜单
├─────────────────────────────────┤
│                                 │
│   第一章 标题                   │
│                                 │
│   正文内容显示在这里...         │  ← E-Ink优化渲染
│   支持手写笔标注和触摸翻页      │
│                                 │
│                                 │
└─────────────────────────────────┘
  ← 点击翻页    点击翻页 →
```

---

## 🚀 快速开始

### 方法1：下载预编译包（推荐）

```bash
# 1. 下载最新版本
# https://github.com/James-Zhu-CA/remarkableweread/releases/latest

# 2. 上传到设备
scp weread-runtime-v3.0.tar.gz root@10.11.99.1:/home/root/

# 3. 安装
ssh root@10.11.99.1
tar -xzf weread-runtime-v3.0.tar.gz
cd weread-runtime-v3.0
./install.sh
```

### 方法2：从源码编译

查看 **[QUICKSTART.md](QUICKSTART.md)** 获取详细指南。

---

## 💡 使用场景

### 为什么选择reMarkable + 微信读书？

- **长时间阅读** - E-Ink屏幕不伤眼，适合长篇阅读
- **专注体验** - 无干扰的阅读环境，沉浸式阅读
- **便携轻薄** - reMarkable Paper Pro仅7.3英寸，随身携带
- **海量书库** - 微信读书百万+图书，随时随地阅读

### 适合人群

- 📖 重度阅读爱好者
- 🎓 学生和研究人员
- 💼 需要长时间阅读的专业人士
- 🌙 夜间阅读用户（E-Ink不发光）

---

## 📊 技术特性

### 性能指标

| 指标 | 数值 |
|------|------|
| 启动时间 | 3-5秒 |
| 内存占用 | 120-180MB |
| 首次加载书籍 | 30-35秒* |
| 字体调整 | 30-35秒* |
| 磁盘缓存 | 100MB |

*首次加载和字体调整需要完整reload页面（React SPA架构限制）

### 技术栈

- **浏览器引擎**: Qt6 WebEngine (Chromium 122)
- **平台**: reMarkable Paper Pro Move (ARM64)
- **屏幕**: 954×1696, 300 PPI
- **优化**: E-Ink专用渲染策略

---

## 🎮 功能说明

### 菜单功能

点击屏幕顶部呼出菜单：

1. **微信读书/得到** - 切换阅读服务
2. **目录** - 快速跳转章节
3. **字体 +** - 增大字体
4. **字体 -** - 缩小字体

### 手势操作

- **翻页**: 点击屏幕左/右侧
- **缩放**: 双指捏合/展开
- **刷新**: 三指下滑
- **退出**: 长按电源键

---

## 🛠️ 系统要求

- **设备**: reMarkable Paper Pro Move
- **存储**: 至少500MB可用空间
- **网络**: Wi-Fi连接（首次登录和下载书籍）
- **SSH**: 已启用SSH访问

---

## 📚 文档

- **[QUICKSTART.md](QUICKSTART.md)** - 快速开始指南
- **[docs/lessons_learned.md](docs/lessons_learned.md)** - 开发经验总结

---

## 🤝 贡献

欢迎提交Issue和Pull Request！

### 开发指南

1. Fork项目
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启Pull Request

---

## 📄 许可证

MIT License - 详见 [LICENSE](LICENSE)

---

## 🙏 致谢

- [Oxide Browser](https://github.com/Eeems/oxide) - Qt WebEngine参考实现
- [reMarkable Community](https://remarkablewiki.com/) - 开发资源和支持
- Qt Project - 优秀的跨平台框架

---

## ⭐ Star History

如果这个项目对你有帮助，请给个Star ⭐️

---

<p align="center">
  Made with ❤️ for reMarkable users
</p>
