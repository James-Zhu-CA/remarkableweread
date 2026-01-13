
版本信息
当前版本：v3.0 (2025-12-15)
主要变更：移除 MXCFB_WAIT_FOR_UPDATE_COMPLETE 阻塞等待，修复看门狗超时问题
备份文件：src/app/main.cpp.v3.0, src/build-cross/WereadEinkBrowser.v3.0
详细变更：见 VERSION_3.0.md


/home/root/weread/logs/weread-viewer-env.log - 环境变量日志

浏览器行为变更（近期）
- 微信读书默认主题/字号改为 DocumentCreation 脚本幂等写入，移除 ensureDefaultBookSettings
- 新增环境变量 WEREAD_NETLOG：设置后追加 Chromium netlog 记录参数
- 关闭 Qt::AA_SynthesizeMouseForUnhandledTouchEvents（改为纯触摸驱动）
- 主窗口显式开启 WA_AcceptTouchEvents，确保触摸事件进入 WebEngine
- 非书籍页仅拦截边缘全局手势，其余触摸事件放行至 WebEngine；focusProxy/QQuickWidget 显式启用触摸并记录
- 书籍页左边缘右滑退出改为直达 https://weread.qq.com/


3.编译浏览器端的命令

docker run --rm -v "$PWD":/workspace -w /workspace ubuntu:22.04 bash -lc "
  set -e
  dpkg --add-architecture arm64 && apt-get update >/dev/null 2>&1
  apt-get install -y qemu-user-static cmake ninja-build build-essential g++-aarch64-linux-gnu \
    libpcre2-16-0:arm64 libnss3:arm64 libnspr4:arm64 libdbus-1-3:arm64 \
    libglib2.0-0:arm64 libharfbuzz0b:arm64 libxkbcommon0:arm64 \
    libwebpdemux2:arm64 libwebpmux3:arm64 libgraphite2-3:arm64 >/dev/null 2>&1
  echo '=== Configuring build ==='
  /workspace/qt6-src/build-qt6/qtbase/bin/qt-cmake -G Ninja -S /workspace/src -B /workspace/src/build-cross \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DCMAKE_PREFIX_PATH=/workspace/qt6-src/build-qt6/qtbase/lib/cmake 2>&1 | tail -5
  echo '=== Building ==='
  cmake --build /workspace/src/build-cross 2>&1 | tail -20
  echo '=== Done ==='
  ls -lh /workspace/src/build-cross/WereadEinkBrowser
"

要点小结（当前已按此方案落地）：

监听服务独立且简单：xovi-tripletap.service 放在 /home/root/xovi-tripletap/，内容仅启动 main.sh（evtest 监听电源键三击→/home/root/xovi/start），不再插手 xochitl 的生命周期，不加 Before=/Restart xochitl 之类会自锁的设置。
自恢复单元放只读根：/usr/lib/systemd/system/xovi-tripletap-restore.service（WantedBy=multi-user.target，After=local-fs + RequiresMountsFor=/home，Type=oneshot）。它在开机后复制 service 到 /etc/systemd/system/，daemon-reload，enable，再 start --no-block，避免 systemd 互锁。
恢复脚本幂等：/home/root/xovi-tripletap/auto-restore.sh 负责复制+enable+start，不检查/重启 xochitl。加时间戳日志到 /home/root/xovi-tripletap/auto-restore.log 便于验证。
持久化方式：启用 restore 单元的 symlink 放在 /usr/lib/systemd/system/multi-user.target.wants/，避免 /etc 易失；服务本体和脚本放 /home，重启后由 restore 自动恢复 /etc 的启用状态。
校验步骤：重启后确认 systemctl status xovi-tripletap-restore.service 已 exited(SUCCESS)，xovi-tripletap.service 为 active(running)。tripletap.log 里应看到 “=== tripletap watcher start…”。
避免的坑：不要在恢复/监听里调用 systemctl restart xochitl，不要在 /home 未挂载前运行，可用 RequiresMountsFor=/home；不要把错误的服务持久化到 /lib/systemd/system 后又加 Before=xochitl 导致自锁/重启循环。


要点梳理（窗口版 = Appload + qtfb 窗口模式）：

manifest 设置：qtfb:true，disablesWindowedMode:false，application 指向窗口版脚本，aspectRatio: "move"，icon 路径可选。
Appload 会分配 QTFB_KEY，启动脚本必须校验并使用它，否则直接退出。
启动脚本环境：QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0:size=954x1696:depth=16（保持 framebuffer 参数）；LD_LIBRARY_PATH 包含 weread/lib、qt6/lib、qt6/lib/nss、nss-libs；WebEngine 资源/locale 路径设好；禁用 sandbox/GPU。
qtfb shim：LD_PRELOAD=/home/root/shims/qtfb-shim.so，配合 QTFB_SHIM_MODE=N_RGB565、QTFB_SHIM_INPUT_MODE=NATIVE、QTFB_SHIM_MODEL=0，并带上 QTFB_KEY。
启动命令：在 weread 目录下 exec ./WereadEinkBrowser "$@"（offscreen+shim 由 qtfb 处理显示为窗口）。
启动前会清理已有 WereadEinkBrowser 进程（WEREAD_KILL_OLD=0 可关闭），避免多进程并确保新版本生效。
日志：给窗口版单独日志，便于排查（如 /home/root/weread/logs/weread-qtfb-window.log）。


要点梳理（窗口版 = Appload + qtfb 窗口模式）：

manifest 设置：qtfb:true，disablesWindowedMode:false，application 指向窗口版脚本，aspectRatio: "move"，icon 路径可选。
Appload 会分配 QTFB_KEY，启动脚本必须校验并使用它，否则直接退出。
启动脚本环境：QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0:size=954x1696:depth=16（保持 framebuffer 参数）；LD_LIBRARY_PATH 包含 weread/lib、qt6/lib、qt6/lib/nss、nss-libs；WebEngine 资源/locale 路径设好；禁用 sandbox/GPU。
qtfb shim：LD_PRELOAD=/home/root/shims/qtfb-shim.so，配合 QTFB_SHIM_MODE=N_RGB565、QTFB_SHIM_INPUT_MODE=NATIVE、QTFB_SHIM_MODEL=0，并带上 QTFB_KEY。
启动命令：在 weread 目录下 exec ./WereadEinkBrowser "$@"（offscreen+shim 由 qtfb 处理显示为窗口）。
启动前会清理已有 WereadEinkBrowser 进程（WEREAD_KILL_OLD=0 可关闭），避免多进程并确保新版本生效。
日志：给窗口版单独日志，便于排查（如 /home/root/weread/logs/weread-qtfb-window.log）。


已将 qtfb-shim.so / qtfb-shim-32bit.so 从 ~/xovi/extensions.d 移到 ~/xovi/shims/，extensions.d 只剩 appload/qt-resource-rebuilder/xovi-message-broker 和 libjpeg/png/webp。
运行 cd /home/root/xovi && ./rebuild_hashtable 成功，生成了 hashtab：/home/root/xovi/exthome/qt-resource-rebuilder/hashtab（大小 655,113 字节）。
重建日志在 ~/xovi/rebuild.log，尾部显示 “Hashtab saved …” 且已自动重新启动 xochitl。
目前 weread 已停止，xochitl 已启动。如果需要继续运行 weread，可以再执行 /home/root/weread/run-weread-drm.sh（先停 xochitl 再启，以避免屏幕抢占）。
