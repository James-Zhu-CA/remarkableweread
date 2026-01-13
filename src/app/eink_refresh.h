#ifndef EINK_REFRESH_H
#define EINK_REFRESH_H

#include <QDebug>
#include <QElapsedTimer>
#include <cstring>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

// Smart E-ink refresh helper with waveform selection (KOReader-style).
// 参考 KOReader framebuffer_mxcfb.lua 和 2.0 后端 FbUpdateTrigger 实现。
// 依赖 appload/qtfb-shim 拦截并路由刷新，因此只需发标准 ioctl。
class EinkRefreshHelper {
public:
  // 波形模式 (参考 KOReader mxcfb_remarkable_h.lua)
  enum WaveformMode {
    WAVE_INIT = 0,  // 初始化
    WAVE_DU = 1,    // Direct Update - 快速，用于高亮/菜单
    WAVE_GC16 = 2,  // Grayscale Clear 16 - 高质量，用于翻页/全刷
    WAVE_GL16 = 3,  // Grayscale Low 16 - 中等质量，用于局部更新
    WAVE_A2 = 4,    // Animation 2 - 极快二值，用于滚动动画
    WAVE_AUTO = 257 // 自动选择
  };

  // 更新模式
  enum UpdateMode {
    MODE_PARTIAL = 0, // 局部更新
    MODE_FULL = 1     // 全屏刷新（带闪烁）
  };

  EinkRefreshHelper();
  ~EinkRefreshHelper();

  // === 高层 API：根据场景自动选择波形 ===
  void refreshFull(int w, int h);
  void refreshPartial(int x, int y, int w, int h);
  void refreshUI(int x, int y, int w, int h);
  void refreshA2(int x, int y, int w, int h);
  void refreshScroll(int w, int h);
  void refreshCleanup(int w, int h);

  bool needsCleanup() const;
  int partialCount() const { return m_partialCount; }
  int a2Count() const { return m_a2Count; }

private:
  struct mxcfb_rect {
    uint32_t top, left, width, height;
  };
  struct mxcfb_update_data {
    mxcfb_rect update_region;
    uint32_t waveform_mode;
    uint32_t update_mode;
    uint32_t update_marker;
    uint32_t temp;
    uint32_t flags;
    struct {
      uint32_t phys_addr;
      uint32_t width;
      uint32_t height;
      uint32_t stride;
      uint32_t pixel_fmt;
    } alt_buffer_data{};
  };
  struct mxcfb_update_marker_data {
    uint32_t update_marker;
    uint32_t collision_test;
  };

  static constexpr unsigned long MXCFB_SEND_UPDATE = 1078478382UL;
  static constexpr unsigned long MXCFB_WAIT_FOR_UPDATE_COMPLETE = 3221767727UL;
  static constexpr int TEMP_USE_AMBIENT = 4096;
  static constexpr int TEMP_USE_REMARKABLE = 24;
  static constexpr int kMaxPartialBeforeCleanup = 10;
  static constexpr int kMaxA2BeforeCleanup = 10;

  void ensureFb();
  void triggerRegion(int x, int y, int w, int h, int wave, int mode,
                     bool waitComplete);
  void resetCounters();

  int m_fd = -1;
  uint32_t m_marker = 0;
  int m_partialCount = 0;
  int m_a2Count = 0;
  QElapsedTimer m_lastRefreshTime;
};

// 保留旧名称兼容性
using FbRefreshHelper = EinkRefreshHelper;

#endif // EINK_REFRESH_H
