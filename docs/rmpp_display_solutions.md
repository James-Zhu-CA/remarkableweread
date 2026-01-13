**路径1：官方 Qt “epaper” 平台插件（官方方案）。**reMarkable 在新版系统中提供了专用的 Qt 图形平台插件 “epaper”，支持 Paper Pro Move 的 1696×954 Canvas Color 显示屏
remarkable.com
。开发者可使用 Qt Quick（Qt 6）编写应用，并通过环境变量和参数将后端切换至电子墨水插件，例如停止内置应用后执行 QT_QUICK_BACKEND=epaper ./app -platform epaper
developer.remarkable.com
。该插件由官方实现，能正确初始化 Chiappa 显示并驱动刷新（包含局部刷新逻辑），适配设备分辨率和硬件特性。**可行性：**极高 – 这是官方推荐路径，已用于 reMarkable 3 代产品。**源码/文档：**官方开发者文档提供使用示例
developer.remarkable.com
developer.remarkable.com
（插件本身闭源，但随SDK提供）。**Chiappa 支持：**官方插件针对Chiappa（Paper Pro Move）设计，支持1696×954全彩帧缓冲及刷新控制。**功能：**可直接由 Qt 应用初始化屏幕、绘制内容，并调用底层刷新（无需显式 ioctl，由插件内部完成）。**集成方式：**作为 Qt Quick 应用的平台插件使用，适合独立Qt前端方案。需注意仅支持 Qt Quick（Qt Widgets 未直接支持）
developer.remarkable.com
。 

**路径2：Qt 通用平台插件（LinuxFB/EGLFS）直接输出。**理论上，可尝试使用 Qt 的 linuxfb 或 EGLFS 平台插件直接渲染到帧缓冲/DRM。但 **可行性：**很低 – Paper Pro Move 上缺省无传统 /dev/fb0 帧缓冲接口，社区反馈在 Paper Pro 上找不到 /dev/fb0 映射
github.com
（意味着系统未公开标准 framebuffer 设备）。内核可能启用了 DRM 驱动但未开启 fbdev 仿真，因此 Qt 的 linuxfb 插件无设备可用。同样，eglfs 需要 GPU/EGL 支持，i.MX93 SoC 仅有2D图形加速，且官方并未提供 EGL 驱动。因此通用 QPA 插件无法直接工作。**源码/文档：**无专门源码，可参考内核配置和 NXP 文档了解 DRM/LCDIF 支持（reMarkable 内核chiappa_defconfig可能禁用了CONFIG_FB，仅使用DRM）。**Chiappa 支持：**若手动启用fbdev仿真并编译内核，FrameBuffer 理论上可映射1696×954显存，但刷新需额外触发。**显示初始化/刷新：**当前固件不提供用户态 ioctl 来刷新帧缓冲；若绕过官方接口自行写显存，将缺乏刷新触发机制（电子墨水需要明确刷新指令）。集成方式：除非通过内核补丁启用fbdev并了解刷新机制，否则不建议走此路径。总体而言，这不是现成可行方案。 


**路径3：第三方帧缓冲转发服务/Shim（rm2fb / rmpp-qtfb-shim）。社区为旧型号开发了 rm2fb（用于 rM2）框架，通过客户端shim +服务进程截获应用对/dev/fb0的访问，并将绘图请求转交给设备的实际屏幕刷新机制
github.com
github.com
。针对 Paper Pro（RMPP，含Move）的新设备，开发者 asivery 提供了开源 rmpp-qtfb-shim
github.com
。该模块利用 Qt 的“qtfb”插件（一个特殊的离屏帧缓冲平台）来绘制，再通过 shim 模拟rM1的fb设备与输入设备
github.com
。KOReader 等第三方应用已经成功使用此方案：按照说明编译 qtfb 插件和 rmpp-qtfb-shim，并将 qtfb-shim.so 注入Paper Pro系统
github.com
。**可行性：**较高 – rmpp-qtfb-shim 已在 Paper Pro (含 Move) 上验证工作，代码已开源
github.com
。**源码链接：**GitHub 项目 asivery/rmpp-qtfb-shim
github.com
（需使用 reMarkable 提供的交叉工具链编译）。**Chiappa 支持：**基本支持。Chiappa与大号Paper Pro在显示接口上一致，只是分辨率不同
remarkable.com
。shim 利用Qt插件获取屏幕信息，1696×954分辨率可通过修改配置或自动检测型号来兼容（如有必要调整深度/调色，KOReader 补丁已考虑rMPP 8bpp问题
fransdejonge.com
）。**显示初始化/刷新：**rmpp-shim通过Qt的qpa插件绘制并调用底层刷新IOCTL（内部由xochitl或系统驱动实现），支持完整刷新，部分刷新取决于底层实现（例如 rmstream 可设置 partial_refresh=true 以利用增量刷新）。**推荐集成：**作为 LD_PRELOAD Shim 随第三方应用加载，或结合 AppLoad/Xochitl扩展 框架运行。该方案适合需要模拟帧缓冲环境的程序（如 KOReader、Plato 等），几乎无需改动应用逻辑。 

**路径4：自研驱动或适配层。**如果希望绕开上述方案，也可考虑研究 reMarkable 提供的内核源码和接口，开发定制的显示适配层。例如，分析 Paper Pro Move 的内核设备树与驱动（i.MX93 显示控制+E-Ink面板）实现一个用户态接口。推测 Chiappa 使用 NXP i.MX93 SoC（双核A55）
github.com
github.com
，面板通过 DRM 面板驱动注册，可能有私有 ioctl 或 sysfs 用于波形刷新。目前无公开文档明确 Chiappa 屏幕控制细节，需从内核源码入手（remarkable/linux-imx-rm 仓库中 chiappa_defconfig 和相关驱动源）或社区经验推断。例如，有人可能通过启用 DRM 的 fbdev 仿真取得 /dev/fb0，然后利用 DRM 原子提交触发刷新。但这需要在内核中打开支持并了解刷新触发属性。**可行性：**实现难度高，需深入内核修改。**源码参考：**reMarkable 提供的 Linux 内核源
github.com
github.com
、NXP i.MX93 显示子系统文档等。**Chiappa 支持：**理论上可针对1696×954编写驱动，结合设备树参数（Canvas Color面板20k色）。**功能：**完全取决于实现，可通过 standalone 服务或 kernel module 提供 /dev/fb 或自定义 ioctl 接口。**集成方式：**需要编译内核模块或固件替换，风险高且破坏官方更新支持，不是首选方案。 综上，当前最可行的路径是官方 Qt epaper 插件（适用于Qt Quick应用）和社区提供的 qtfb-shim 兼容层（适用于非Qt应用）。前者由官方维护，开箱即用；后者有现成源码，可小改动支持Chiappa屏幕。
developer.remarkable.com
github.com
两种方案均利用了系统内置的显示刷新机制，确保支持Chiappa的分辨率和刷新IOCTL。相比之下，试图直接操作帧缓冲或自行编写驱动需要较大工作量，不如利用已有代码成果来快速实现基本显示功能。
引用

reMarkable Paper Pro | reMarkable

https://remarkable.com/products/remarkable-paper/pro/details/compare

reMarkable logo

https://developer.remarkable.com/documentation/qt_epaper

reMarkable logo

https://developer.remarkable.com/documentation/qt_epaper

reMarkable logo

https://developer.remarkable.com/documentation/qt_epaper

Support for reMarkable Paper Pro · Issue #117 - GitHub

https://github.com/owulveryck/goMarkableStream/issues/117

GitHub - ddvk/remarkable2-framebuffer: remarkable2 framebuffer reversing

https://github.com/ddvk/remarkable2-framebuffer

GitHub - ddvk/remarkable2-framebuffer: remarkable2 framebuffer reversing

https://github.com/ddvk/remarkable2-framebuffer

GitHub - asivery/rmpp-qtfb-shim: A shim for emulating the rM1 framebuffer and input devices on rMPP thanks to qtfb

https://github.com/asivery/rmpp-qtfb-shim

GitHub - asivery/rmpp-qtfb-shim: A shim for emulating the rM1 framebuffer and input devices on rMPP thanks to qtfb

https://github.com/asivery/rmpp-qtfb-shim

Installation on reMarkable Paper Pro · Issue #13678 · koreader/koreader · GitHub

https://github.com/koreader/koreader/issues/13678

koreader | The One with the Thoughts of Frans

https://fransdejonge.com/tag/koreader/

GitHub - reMarkable/linux-imx-rm: Linux kernel for reMarkable Paper Pro

https://github.com/reMarkable/linux-imx-rm

GitHub - reMarkable/linux-imx-rm: Linux kernel for reMarkable Paper Pro

https://github.com/reMarkable/linux-imx-rm

GitHub - reMarkable/linux-imx-rm: Linux kernel for reMarkable Paper Pro

https://github.com/reMarkable/linux-imx-rm

GitHub - reMarkable/linux-imx-rm: Linux kernel for reMarkable Paper Pro

https://github.com/reMarkable/linux-imx-rm