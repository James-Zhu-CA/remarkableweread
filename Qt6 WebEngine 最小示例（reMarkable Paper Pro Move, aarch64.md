Qt6 WebEngine 最小示例（reMarkable Paper Pro Move, aarch64）
以下内容展示一个基于 Qt6 的 Qt WebEngine 跨平台最小示例项目，目标是在 reMarkable Paper Pro Move 彩色墨水屏设备（i.MX93，无GPU）上全屏显示微信读书网页版。示例包括项目结构、关键代码、Docker 下交叉编译指南、运行启动脚本、针对 E-Ink 优化的 CSS 片段，以及在无 GPU 环境下建议使用的 Chromium 参数。
项目结构与代码
假设项目目录名为 weread-poc，其结构如下：
weread-poc/
├── CMakeLists.txt       (CMake 构建脚本)
├── main.cpp             (应用程序入口，初始化 Qt 和加载 QML)
└── main.qml             (QML 界面，包含 WebEngineView)
main.cpp（入口程序）:
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtWebEngineQuick>

int main(int argc, char *argv[]) {
    // 启用 OpenGL 上下文共享，并初始化 Qt WebEngine（必须在创建 QGuiApplication 之前）[1]
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QtWebEngineQuick::initialize();  // 初始化 WebEngine (Qt6)

    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty()) 
        return -1;
    return app.exec();
}
说明: 根据 Qt 文档，使用 QML 中的 WebEngineView 组件前需要在 main() 中调用 QtWebEngineQuick::initialize() 进行初始化[1]。上面我们在创建 QGuiApplication 之前设置了 Qt::AA_ShareOpenGLContexts 属性并调用初始化，这与官方示例要求一致[2]。
main.qml（界面定义）:
import QtQuick 2.15
import QtQuick.Window 2.15
import QtWebEngine 1.10

Window {
    visible: true
    visibility: "FullScreen"     // 窗口全屏显示
    color: "white"              // 背景白色，适合阅读（可根据需求调整）
    WebEngineView {
        id: webview
        anchors.fill: parent
        url: "https://weread.qq.com/web"  // 加载微信读书网页版
    }
}
说明: 上述 QML 创建了一个无边框全屏窗口，并在其中放置一个 WebEngineView 元件填满整个屏幕。这样应用启动后即全屏显示指定网址的网页内容。
CMakeLists.txt（项目构建脚本）:
cmake_minimum_required(VERSION 3.16)
project(WereadEinkBrowser LANGUAGES CXX)

# 找到 Qt6 所需模块：Quick (Qt Quick) 和 WebEngineQuick (Qt WebEngine QML 类型)
find_package(Qt6 REQUIRED COMPONENTS Quick WebEngineQuick)

# 添加可执行目标
qt_add_executable(${PROJECT_NAME}
    main.cpp
)

# 将 QML 文件打包到资源（可选，也可以直接从文件系统加载）
qt_add_qml_module(${PROJECT_NAME}
    URI WereadBrowser       # 定义 QML 模块名字
    VERSION 1.0
    QML_FILES main.qml
)

# 链接 Qt6 库
target_link_libraries(${PROJECT_NAME} PRIVATE 
    Qt6::Quick 
    Qt6::WebEngineQuick
)
上述 CMake 配置会生成可执行文件 WereadEinkBrowser，并将 main.qml 内联到应用资源中（也可以根据需要改为从文件直接加载）。
Docker 下的交叉编译设置
在 Docker 中交叉编译 aarch64（ARM64）版本应用，需要使用适用于目标设备的交叉工具链和 Qt 库。reMarkable 官方提供了对应型号的 Yocto SDK，它包含 aarch64 编译器、sysroot（含所需头文件和库）等[3][4]。reMarkable Paper Pro Move 的内部代号为 “chiappa”[5]，应下载对应版本的 SDK 安装包（文件名包含 chiappa）。假设已获取 SDK 安装脚本（例如 meta-toolchain-remarkable-<版本>-chiappa-public-x86_64-toolchain.sh），可按照以下步骤在 Docker 容器内安装并使用：
Dockerfile 示例（截取核心部分）:
FROM ubuntu:22.04

# 安装交叉编译所需基础包
RUN apt-get update && apt-get install -y build-essential cmake git wget

# 拷贝 reMarkable SDK 安装脚本进入容器
COPY meta-toolchain-remarkable-*-chiappa-*-toolchain.sh /tmp/sdk-install.sh

# 安装 reMarkable Paper Pro Move (chiappa) SDK 到 /opt/remarkable-sdk
RUN chmod +x /tmp/sdk-install.sh && /tmp/sdk-install.sh -d /opt/remarkable-sdk

# 设置环境变量，指向 SDK 环境设置脚本
ENV SDK_ENV=/opt/remarkable-sdk/environment-setup-cortexa55-crypto-remarkable-linux
以上 Docker 镜像包含了交叉编译所需的编译器和 SDK。接下来，我们可以在容器中执行构建命令。为了方便，可以编写一个脚本在容器内完成 CMake 配置与编译：
build.sh（使用 Docker 进行交叉编译的脚本）:
#!/bin/bash
# 构建 Docker 镜像（假定 Dockerfile 在当前目录）
docker build -t remarkable-qt6-build .

# 在容器中执行编译
docker run --rm -v "$PWD":/workspace -w /workspace remarkable-qt6-build bash -c "\
    source $SDK_ENV && \
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j$(nproc)"
"
运行上述 build.sh 脚本，将会使用我们准备的 Docker 镜像，在其中加载 reMarkable 的交叉编译环境，然后执行 CMake 生成和编译步骤。关键点是通过 source $SDK_ENV 加载 SDK 的环境设置脚本，这将设置合适的交叉编译工具链前缀、CC/CXX 等变量，以及指向 ARM64 sysroot 的路径[6]。一旦环境就绪，cmake 会检测到 Qt6 (Quick, WebEngine 等) 库和头文件（来自 SDK 的 sysroot）并生成 Makefile，随后 cmake --build 产生 ARM64 平台的可执行文件。
提示: 确保 SDK 环境中包含 Qt6 WebEngine 模块的开发头文件与库。如果 reMarkable 提供的系统不含 QtWebEngine，可能需要自行构建 Qt6 WebEngine for ARM，或将其集成到 sysroot 中。通常，reMarkable 的系统自带 Qt Quick 框架用于其界面应用，因此 SDK 中应有对应的 Qt 库可供链接。
运行启动脚本 (run.sh)
在将编译好的应用部署到 reMarkable 设备上运行时，需要配置一些环境变量以适配 E-Ink 硬件及无 GPU 的运行环境。以下是一个示例启动脚本 run.sh，负责设置平台和 WebEngine 所需的环境，然后启动应用：
#!/bin/sh

# 指定 Qt 使用 Linux 帧缓冲平台插件（直接渲染到 /dev/fb0）
export QT_QPA_PLATFORM=linuxfb

# 禁用 Qt WebEngine 沙箱（嵌入式/无权限隔离环境下需要关闭）[7]
export QTWEBENGINE_DISABLE_SANDBOX=1

# 强制使用 Qt Quick 软件渲染适配（i.MX93 无 OpenGL GPU，仅有2D加速）[8]
export QT_QUICK_BACKEND=software

# 设置 Chromium 内核参数：禁用 GPU 加速及平滑滚动等（详见下文）
export QTWEBENGINE_CHROMIUM_FLAGS="--disable-gpu --disable-gpu-compositing --disable-smooth-scrolling --disable-accelerated-2d-canvas --disable-webgl"

# 启动应用
./WereadEinkBrowser
上述环境变量含义如下：
QT_QPA_PLATFORM=linuxfb: 使用 Linux 帧缓冲平台插件。在 reMarkable 等无 X11/Wayland 环境下，直接通过 framebuffer 输出图形。对于墨水屏，这种方式常用且可结合设备提供的刷新机制插件（如 qtfb 模块）实现部分刷新。
QTWEBENGINE_DISABLE_SANDBOX=1: 禁用 WebEngine 的沙箱。嵌入式设备通常以 root 身份运行应用，Chromium 沙箱在这种情况下会启动失败，因此需要通过环境变量或 --no-sandbox 参数关闭沙箱限制[7]。
QT_QUICK_BACKEND=software: 强制 Qt Quick 使用软件渲染模式，而非 OpenGL（因为 i.MX93 平台没有支持 OpenGL 的 GPU，仅提供基本的2D图形加速单元）[8]。这保证 QML 界面在无 GPU 的设备上正常显示。【注】Qt6 默认启用了 RHI渲染抽象，可自动选择可用后台；但明确指定 software 渲染可避免Qt尝试加载不存在的 OpenGL/EGL驱动。
QTWEBENGINE_CHROMIUM_FLAGS: 传递给 Chromium 浏览器内核的启动参数，用于优化在无GPU/弱性能环境下的表现（见下一节推荐的具体 flags）。例如这里禁用了 GPU 硬件加速和页面平滑滚动等特性，以降低CPU消耗并避免无效的GPU调用。
在设备上执行 run.sh，即可设置好环境并运行浏览器应用。确保设备已开启开发者模式并能访问 shell，然后将可执行文件及所需 Qt 库部署到设备对应路径，运行启动脚本进行测试。
针对微信读书网页版的 E-Ink 优化 CSS
E-Ink 屏幕刷新率低且不适合频繁动画。我们可以注入一些 CSS 样式来优化微信读书网页版在墨水屏上的显示效果，包括禁用动画、禁用平滑滚动以及增大字体等。以下是一个名为 optimized.css 的示例样式：
/* 1. 禁用一切 CSS 动画和过渡效果，减少闪烁和残影 */
* {
    animation: none !important;
    transition: none !important;
}

/* 2. 关闭滚动行为的平滑效果，避免滚动时的拖影和延迟 */
html {
    scroll-behavior: auto !important;
}

/* 3. 提高全局字体大小，增强墨水屏上的阅读清晰度 */
html, body {
    font-size: 18px !important;  /* 根据需要调整字号，例如18px或120% */
}
上述规则涵盖整个页面，将任何元素的动画和过渡属性一律清除[9]。通过对 html 设置 scroll-behavior: auto !important 来强制禁用 CSS 平滑滚动，以确保调用浏览器滚动时直接跳转[10]（平滑滚动在墨水屏上效果不佳）。同时，全局增大字体方便在高分辨率墨水屏上阅读（这里以18px为例，可根据实际需要调整）。
注入方法: 可以在加载页面时通过 Qt WebEngine 的用户脚本功能将上述 CSS 注入页面。例如，在 QML 中利用 WebEngineView 的 userScripts 属性，创建一个 WebEngineScript 在文档加载完成时插入 <style> 节点来应用这些CSS[11]。也可以使用 webview.runJavaScript(...) 执行JS代码添加样式表。在我们的 PoC 中，可将 optimized.css 文件读入字符串，并在页面打开后注入。
无 GPU 环境的 Chromium 参数建议
为了在无 GPU/硬件加速的设备（如 i.MX93）上提升 Qt WebEngine (Chromium) 的运行效率，建议启动时添加以下 Chromium flags（通过环境变量 QTWEBENGINE_CHROMIUM_FLAGS 传递）：
--disable-gpu – 禁用GPU加速。强制Chromium完全使用软件渲染，避免调用不存在的GPU功能[12]。由于 i.MX93 仅有基本2D图形单元、不支持OpenGL[8]，必须关闭GPU相关功能。
--disable-gpu-compositing – 禁用GPU合成。使用软件进行页面合成渲染，减少对GPU的依赖（与--disable-gpu配合保证所有渲染路径都走CPU）。
--disable-accelerated-2d-canvas – 禁用Canvas的硬件加速。Canvas绘图改用软件实现，以防止调用不可用的加速路径。
--disable-webgl – 禁用 WebGL。防止网页尝试启用 WebGL（3D 图形），在无GPU环境下避免性能浪费或潜在错误。
--disable-smooth-scrolling – 禁用平滑滚动。确保页面滚动立即生效，搭配前述 CSS 进一步防止墨水屏上滚动拖影（即使网页或浏览器默认启用了平滑滚动，此标志也可全局关闭该特性）。
--disable-accelerated-video-decode – 禁用视频解码加速。避免调用硬件视频解码器（如果设备无对应硬件或驱动），以防止视频内容造成性能问题。（微信读书主要以文本为主，可按需决定是否加入此项）
将上述参数拼接成字符串赋给环境变量 QTWEBENGINE_CHROMIUM_FLAGS 即可（如前面 run.sh 所示）。这些 Flag 会在 Qt WebEngine 初始化 Chromium 内核时生效，把浏览器设置为纯软件渲染模式，减少不必要的渲染开销和可能的兼容性问题。
参考: Qt 官方文档指出，可以通过设置 QTWEBENGINE_CHROMIUM_FLAGS="--disable-gpu" 来明确禁用硬件加速渲染，以适应无法使用 GPU 的环境[12]。另外，在 Linux 上如果需要彻底关闭 Chromium 沙箱，也可以加入 --no-sandbox 参数或对应环境变量[7]（本示例中已通过 QTWEBENGINE_DISABLE_SANDBOX=1 设置）。

通过以上步骤，我们构建了一个最小的 Qt6 WebEngine 示例应用，并针对 reMarkable Paper Pro Move 设备做了专门优化。应用将全屏呈现微信读书网页版，利用CSS和Chromium参数改善墨水屏的浏览体验。整个项目结构清晰，代码量精简，可作为后续开发更完整功能的基础。您可以根据需要进一步扩展，例如实现触控翻页、离线缓存等功能。祝您在 reMarkable 墨水屏上阅读愉快！
参考来源:
Qt 官方文档: “WebEngineView QML Type”[2][13] – 展示了在 QML 中使用 WebEngineView 时需要在 C++ 主函数中调用初始化的示例。
reMarkable 开发者文档: “Software Development Kits”[14][5] – 提供了不同产品代号（如 chiappa）对应的设备型号说明，以及交叉编译 SDK 的使用方法[6]。
NXP 社区论坛: “does the i.mx93 support opengl?”[8] – 说明了 i.MX93 平台仅有2D GPU且不支持 OpenGL，需使用软件渲染方案。
Qt Platform Notes 文档: 关于禁用沙箱及禁用GPU的环境变量设置[7][12]。
前端开发资料: Nelson Figueroa 博客[9]和 Stack Overflow 答案[10] – 提供了禁用 CSS 动画/过渡以及强制关闭平滑滚动的 CSS 技巧，对于优化网页在墨水屏上的呈现很有帮助。

[1] [2] [11] [13] WebEngineView QML Type | Qt WebEngine | Qt 6.10.0
https://doc.qt.io/qt-6/qml-qtwebengine-webengineview.html
[3] [4] [5] [6] [14] reMarkable logo
https://developer.remarkable.com/documentation/sdk
[7] Qt WebEngine Platform Notes | Qt WebEngine | Qt 6.10.0
https://doc.qt.io/qt-6/qtwebengine-platform-notes.html
[8] does the i.mx93 support opengl? - NXP Community
https://community.nxp.com/t5/i-MX-Processors/does-the-i-mx93-support-opengl/td-p/1751420
[9] How to Disable CSS Animations and Transitions | Nelson Figueroa
https://nelson.cloud/how-to-disable-css-animations-and-transitions/
[10] javascript - Is there a way to force scroll behavior `auto` even if the scroll behavior is defined `smooth` on the html? - Stack Overflow
https://stackoverflow.com/questions/64423328/is-there-a-way-to-force-scroll-behavior-auto-even-if-the-scroll-behavior-is-de
[12] Qt WebEngine Features | Qt WebEngine | Qt 6.10.0
https://doc.qt.io/qt-6/qtwebengine-features.html