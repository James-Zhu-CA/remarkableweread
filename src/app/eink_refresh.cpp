#include "eink_refresh.h"
#include <cerrno>

EinkRefreshHelper::EinkRefreshHelper() {
  ensureFb();
  m_fd = ::open("/dev/fb0", O_RDWR);
  if (m_fd < 0) {
    qWarning() << "[EINK] open fb0 failed" << strerror(errno);
  } else {
    qInfo() << "[EINK] fb0 opened, smart refresh enabled";
  }
  m_lastRefreshTime.start();
}

EinkRefreshHelper::~EinkRefreshHelper() {
  if (m_fd >= 0)
    ::close(m_fd);
}

void EinkRefreshHelper::refreshFull(int w, int h) {
  if (qEnvironmentVariableIsSet("WEREAD_EINK_DEBUG")) {
    qInfo() << "[EINK] Full refresh (GC16 FULL)" << w << "x" << h;
  }
  triggerRegion(0, 0, w, h, WAVE_GC16, MODE_FULL, false);
  resetCounters();
}

void EinkRefreshHelper::refreshPartial(int x, int y, int w, int h) {
  if (qEnvironmentVariableIsSet("WEREAD_EINK_DEBUG")) {
    qInfo() << "[EINK] Partial refresh (GL16)" << x << y << w << h;
  }
  triggerRegion(x, y, w, h, WAVE_GL16, MODE_PARTIAL, false);
  m_partialCount++;
}

void EinkRefreshHelper::refreshUI(int x, int y, int w, int h) {
  if (qEnvironmentVariableIsSet("WEREAD_EINK_DEBUG")) {
    qInfo() << "[EINK] UI refresh (DU)" << x << y << w << h;
  }
  triggerRegion(x, y, w, h, WAVE_DU, MODE_PARTIAL, false);
  m_partialCount++;
}

void EinkRefreshHelper::refreshA2(int x, int y, int w, int h) {
  if (qEnvironmentVariableIsSet("WEREAD_EINK_DEBUG")) {
    qInfo() << "[EINK] A2 refresh (fast scroll)" << x << y << w << h;
  }
  triggerRegion(x, y, w, h, WAVE_A2, MODE_PARTIAL, false);
  m_a2Count++;
}

void EinkRefreshHelper::refreshScroll(int w, int h) {
  qint64 elapsed = m_lastRefreshTime.elapsed();
  m_lastRefreshTime.restart();

  if (elapsed < 200) {
    if (qEnvironmentVariableIsSet("WEREAD_EINK_DEBUG")) {
      qInfo() << "[EINK] Fast scroll (A2)" << elapsed << "ms since last";
    }
    triggerRegion(0, 0, w, h, WAVE_A2, MODE_PARTIAL, false);
    m_a2Count++;
  } else {
    if (qEnvironmentVariableIsSet("WEREAD_EINK_DEBUG")) {
      qInfo() << "[EINK] Normal scroll (GL16)" << elapsed << "ms since last";
    }
    triggerRegion(0, 0, w, h, WAVE_GL16, MODE_PARTIAL, false);
    m_partialCount++;
  }
}

void EinkRefreshHelper::refreshCleanup(int w, int h) {
  qInfo() << "[EINK] Cleanup refresh (INIT FULL) - most thorough refresh for "
             "clearing ghosting";
  triggerRegion(0, 0, w, h, WAVE_INIT, MODE_FULL, false);
  resetCounters();
}

bool EinkRefreshHelper::needsCleanup() const {
  return m_partialCount >= kMaxPartialBeforeCleanup ||
         m_a2Count >= kMaxA2BeforeCleanup;
}

void EinkRefreshHelper::ensureFb() {
  struct stat st {};
  if (::stat("/dev/fb0", &st) == 0 && S_ISCHR(st.st_mode))
    return;
  if (::mknod("/dev/fb0", S_IFCHR | 0666, makedev(29, 0)) != 0) {
    qWarning() << "[EINK] mknod /dev/fb0 failed" << strerror(errno);
  } else {
    ::chmod("/dev/fb0", 0666);
    qInfo() << "[EINK] created /dev/fb0";
  }
}

void EinkRefreshHelper::triggerRegion(int x, int y, int w, int h, int wave,
                                      int mode, bool waitComplete) {
  if (m_fd < 0)
    return;

  mxcfb_update_data upd{};
  upd.update_region.left = static_cast<uint32_t>(x);
  upd.update_region.top = static_cast<uint32_t>(y);
  upd.update_region.width = static_cast<uint32_t>(w);
  upd.update_region.height = static_cast<uint32_t>(h);
  upd.waveform_mode = static_cast<uint32_t>(wave);
  upd.update_mode = static_cast<uint32_t>(mode);
  upd.temp = (wave == WAVE_DU) ? TEMP_USE_REMARKABLE : TEMP_USE_AMBIENT;
  upd.flags = 0;
  upd.update_marker = ++m_marker;

  const char *waveStr = (wave == WAVE_INIT)   ? "INIT"
                        : (wave == WAVE_GC16) ? "GC16"
                        : (wave == WAVE_GL16) ? "GL16"
                        : (wave == WAVE_DU)   ? "DU"
                        : (wave == WAVE_A2)   ? "A2"
                                              : "AUTO";
  const char *modeStr = (mode == MODE_FULL) ? "FULL" : "PARTIAL";

  if (::ioctl(m_fd, MXCFB_SEND_UPDATE, &upd) != 0) {
    qWarning() << "[EINK] send failed" << strerror(errno);
    return;
  }

  if (waitComplete) {
    mxcfb_update_marker_data md{};
    md.update_marker = upd.update_marker;
    md.collision_test = 0;
    if (::ioctl(m_fd, MXCFB_WAIT_FOR_UPDATE_COMPLETE, &md) != 0) {
      qWarning() << "[EINK] wait failed" << strerror(errno);
    }
  }

  if (qEnvironmentVariableIsSet("WEREAD_EINK_DEBUG")) {
    qInfo() << "[EINK]" << waveStr << modeStr << "region" << x << y << w << h
            << "marker" << upd.update_marker
            << (waitComplete ? "(waited)" : "");
  }
}

void EinkRefreshHelper::resetCounters() {
  m_partialCount = 0;
  m_a2Count = 0;
}
