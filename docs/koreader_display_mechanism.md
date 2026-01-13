KOReader 在 reMarkable 设备上的显示实现架构
1. 设备检测与初始化
frontend/device/remarkable/device.lua
第34行：检测是否使用 qtfb-shim
local is_qtfb_shimmed = (os.getenv("LD_PRELOAD") or ""):find("qtfb%-shim") ~= nil
第211行：初始化 framebuffer 对象
self.screen = require("ffi/framebuffer_mxcfb"):new{device = self, debug = logger.dbg}
第453-472行：设备类型验证
RM2/RMPP/RMPPM 都必须使用 qtfb-shim
RMPP/RMPPM 必须设置 QTFB_SHIM_MODE=N_RGB565
2. 核心 Framebuffer 实现
ffi/framebuffer_mxcfb.lua - 60KB，核心显示逻辑
刷新 API（第779-812行）
refreshPartialImp(x, y, w, h, dither) - 部分刷新（快速）
refreshFullImp(x, y, w, h, dither) - 完整刷新（闪烁）
refreshUIImp(x, y, w, h, dither) - UI 模式刷新
refreshFastImp(x, y, w, h, dither) - 快速刷新（DU波形）
refreshA2Imp(x, y, w, h, dither) - A2 刷新（最快，质量最低）
reMarkable 刷新函数（第754-761行）
local function refresh_remarkable(fb, is_flashing, waveform_mode, x, y, w, h)
    if waveform_mode == C.WAVEFORM_MODE_DU then
       fb.update_data.temp = C.TEMP_USE_REMARKABLE  -- 使用固定温度24°C以降低延迟
    else
       fb.update_data.temp = C.TEMP_USE_AMBIENT     -- 使用环境温度
    end
    return mxc_update(fb, C.MXCFB_SEND_UPDATE, fb.update_data, is_flashing, waveform_mode, x, y, w, h)
end
核心刷新逻辑（第273-479行）
mxc_update() 函数执行：
边界检查和坐标旋转（第283-294行）
等待前一个 marker 完成（第323-351行）
关键：第425行执行 ioctl 系统调用
C.ioctl(fb.fd, C.MXCFB_SEND_UPDATE, ioc_data)
等待刷新完成（第439-448行）
reMarkable 初始化（第1139-1161行）
elseif self.device:isRemarkable() then
    require("ffi/mxcfb_remarkable_h")
    
    self.mech_refresh = refresh_remarkable
    self.mech_wait_update_complete = remarkable_mxc_wait_for_update_complete
    
    -- 波形模式配置
    self.waveform_a2 = C.WAVEFORM_MODE_A2          -- 最快（质量最低）
    self.waveform_fast = C.WAVEFORM_MODE_DU        -- 快速
    self.waveform_ui = C.WAVEFORM_MODE_GL16        -- UI 刷新
    self.waveform_flashui = C.WAVEFORM_MODE_GC16   -- 闪烁 UI
    self.waveform_full = C.WAVEFORM_MODE_GC16      -- 完整刷新
    self.waveform_partial = C.WAVEFORM_MODE_GL16   -- 部分刷新
3. MXCFB ioctl 定义
ffi/mxcfb_remarkable_h.lua - FFI C 结构定义
// 核心数据结构（第31-53行）
struct mxcfb_rect {
    unsigned int top, left, width, height;
};

struct mxcfb_update_data {
    struct mxcfb_rect update_region;
    unsigned int waveform_mode;      // 波形模式
    unsigned int update_mode;        // PARTIAL/FULL
    unsigned int update_marker;      // 刷新标记
    int temp;                        // 温度
    unsigned int flags;              // 标志位
    int dither_mode;                 // 抖动模式
    int quant_bit;
    struct mxcfb_alt_buffer_data alt_buffer_data;
};

// ioctl 命令（第58-59行）
static const int MXCFB_SEND_UPDATE = 1078478382;               // 发送刷新
static const int MXCFB_WAIT_FOR_UPDATE_COMPLETE = 3221767727;  // 等待完成
4. 等待刷新完成
ffi/framebuffer_mxcfb.lua - reMarkable 等待函数
local function remarkable_mxc_wait_for_update_complete(fb, marker)
    -- 等待特定更新完成
    fb.marker_data.update_marker = marker
    return C.ioctl(fb.fd, C.MXCFB_WAIT_FOR_UPDATE_COMPLETE, fb.marker_data)
end
5. 启动脚本配置
koreader.sh - Shell 启动脚本
第76-86行：读取原始 framebuffer 设置
ORIG_FB_ROTA="$(./fbdepth -o)"   # 获取旋转
ORIG_FB_BPP="$(./fbdepth -g)"    # 获取位深度
第100-120行：ko_do_fbdepth() 函数
./fbdepth -d 8 -r 1    # 切换到8bpp + Portrait旋转
第142行：启动主程序
./reader.lua "$@" >>crash.log 2>&1
显示流程总结
┌────────────────────────────────────────────────────────────┐
│ KOReader 调用 refreshPartialImp(x, y, w, h)              │
└───────────────────────┬────────────────────────────────────┘
                        ↓
┌────────────────────────────────────────────────────────────┐
│ refresh_remarkable() 设置波形模式和温度                    │
└───────────────────────┬────────────────────────────────────┘
                        ↓
┌────────────────────────────────────────────────────────────┐
│ mxc_update() 准备 mxcfb_update_data 结构                  │
└───────────────────────┬────────────────────────────────────┘
                        ↓
┌────────────────────────────────────────────────────────────┐
│ ioctl(fd, MXCFB_SEND_UPDATE, update_data)                 │
│ ↓ 直接写入 /dev/fb0 framebuffer 设备                      │
└───────────────────────┬────────────────────────────────────┘
                        ↓
         ╔═════════════════════════════╗
         ║ 如果配置了 LD_PRELOAD       ║
         ╚═════════════╦═══════════════╝
                       ↓
┌────────────────────────────────────────────────────────────┐
│ qtfb-shim.so 拦截 ioctl 调用                              │
│ (来自 rm-appload 项目，非 KOReader 代码)                  │
└───────────────────────┬────────────────────────────────────┘
                        ↓
┌────────────────────────────────────────────────────────────┐
│ qtfb-server 通过共享内存接收并执行真正的刷新               │
│ (来自 rm-appload 项目，非 KOReader 代码)                  │
└────────────────────────────────────────────────────────────┘
关键发现
KOReader 本身只操作标准 framebuffer：通过 /dev/fb0 和 MXCFB ioctl 接口
没有 qtfb-server/shim 实现：这些完全来自外部的 rm-appload 项目
显示转发由 LD_PRELOAD 实现：在环境变量中配置 qtfb-shim.so 拦截 ioctl
共享内存通信：qtfb-shim 和 qtfb-server 之间通过共享内存传递 framebuffer 数据
温度优化：DU 模式使用固定 24°C 而非环境温度，以降低刷新延迟
这个架构设计非常巧妙：KOReader 保持标准 framebuffer 实现，无需修改即可在 Move 设备上通过 shim 层转发显示。