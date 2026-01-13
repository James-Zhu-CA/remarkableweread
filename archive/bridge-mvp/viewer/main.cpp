#define _POSIX_C_SOURCE 200809L

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QTimer>
#include <QImage>
#include <QDebug>
#include <QFile>
#include <QByteArray>
#include <QScopedPointer>
#include <QUdpSocket>
#include <QHostAddress>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QScreen>
#include <QVector>
#include <QtMath>
#include <QElapsedTimer>
#include <QDateTime>
#include <QProcess>
#include <functional>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>

#include "../shm_proto.h"

static const char kViewerVersion[] = "2.0-shim";

// Smart MXCFB trigger with waveform selection and partial refresh support.
class FbUpdateTrigger {
public:
    // Waveform modes for different scenarios
    enum WaveformMode {
        WAVE_GC16 = 2,   // High quality grayscale, slower, no ghosting
        WAVE_A2 = 6,     // Fast binary, for animations/scrolling, may ghost
        WAVE_DU = 1,     // Direct update, medium speed
        WAVE_AUTO = 257  // Let hardware decide
    };
    
    // Update modes
    enum UpdateMode {
        MODE_PARTIAL = 0,  // Partial update (局刷)
        MODE_FULL = 1      // Full update (全刷)
    };

    FbUpdateTrigger() {
        ensureFbDevice();
        fd = ::open("/dev/fb0", O_RDWR);
        if (fd < 0) {
            qWarning() << "[FBUPD] open fb0 failed" << strerror(errno);
        } else {
            const QByteArray ldPreload = qgetenv("LD_PRELOAD");
            const QByteArray qtfbKey = qgetenv("QTFB_KEY");
            qInfo() << "[FBUPD] fb0 opened ok" << "fd" << fd
                    << "LD_PRELOAD" << ldPreload
                    << "QTFB_KEY" << qtfbKey;
        }
    }
    ~FbUpdateTrigger() {
        if (fd >= 0) ::close(fd);
    }
    
    // Legacy full-screen GC16 refresh
    void trigger(int w, int h) {
        triggerRegion(0, 0, w, h, WAVE_GC16, MODE_FULL);
    }
    
    // Smart region update with waveform selection
    void triggerRegion(int x, int y, int w, int h, WaveformMode wave, UpdateMode mode) {
        if (fd < 0) return;
        mxcfb_update_data upd{};
        upd.update_region.left = static_cast<uint32_t>(x);
        upd.update_region.top = static_cast<uint32_t>(y);
        upd.update_region.width = static_cast<uint32_t>(w);
        upd.update_region.height = static_cast<uint32_t>(h);
        upd.waveform_mode = static_cast<uint32_t>(wave);
        upd.update_mode = static_cast<uint32_t>(mode);
        upd.temp = TEMP_USE_AMBIENT;
        upd.flags = 0;
        upd.update_marker = ++marker;
        
        const char* waveStr = (wave == WAVE_GC16) ? "GC16" : 
                              (wave == WAVE_A2) ? "A2" : 
                              (wave == WAVE_DU) ? "DU" : "AUTO";
        const char* modeStr = (mode == MODE_FULL) ? "FULL" : "PARTIAL";
        
        if (::ioctl(fd, MXCFB_SEND_UPDATE, &upd) != 0) {
            if (++failCount <= 5) {
                qWarning() << "[FBUPD] send failed" << strerror(errno);
            }
            return;
        }
        mxcfb_update_marker_data m{};
        m.update_marker = upd.update_marker;
        if (::ioctl(fd, MXCFB_WAIT_FOR_UPDATE_COMPLETE, &m) != 0) {
            if (++failWait <= 5) {
                qWarning() << "[FBUPD] wait failed" << strerror(errno);
            }
        } else {
            ++okCount;
            // Log every update for debugging smart refresh
            qInfo() << "[FBUPD]" << waveStr << modeStr
                    << "region" << x << y << w << h
                    << "marker" << m.update_marker << "count" << okCount;
        }
    }
    
    // Quick A2 partial refresh for scrolling
    void triggerA2Partial(int x, int y, int w, int h) {
        triggerRegion(x, y, w, h, WAVE_A2, MODE_PARTIAL);
    }
    
    // GC16 partial refresh for small changes
    void triggerGC16Partial(int x, int y, int w, int h) {
        triggerRegion(x, y, w, h, WAVE_GC16, MODE_PARTIAL);
    }
    
    // Full screen GC16 cleanup
    void triggerFullCleanup(int w, int h) {
        triggerRegion(0, 0, w, h, WAVE_GC16, MODE_FULL);
    }

private:
    void ensureFbDevice() {
        struct stat st{};
        if (::stat("/dev/fb0", &st) == 0 && S_ISCHR(st.st_mode)) {
            return;
        }
        // Attempt to create fb0 (29,0) similar to Appload shim launcher
        if (::mknod("/dev/fb0", S_IFCHR | 0666, makedev(29, 0)) != 0) {
            qWarning() << "[FBUPD] mknod /dev/fb0 failed" << strerror(errno);
        } else {
            ::chmod("/dev/fb0", 0666);
            qInfo() << "[FBUPD] created /dev/fb0 node (29,0)";
        }
    }
    struct mxcfb_rect { uint32_t top, left, width, height; };
    struct mxcfb_alt_buffer_data {
        uint32_t phys_addr;
        uint32_t width;
        uint32_t height;
        uint32_t stride;
        uint32_t pixel_fmt;
    };
    struct mxcfb_update_data {
        mxcfb_rect update_region;
        uint32_t waveform_mode;
        uint32_t update_mode;
        uint32_t update_marker;
        uint32_t temp;
        uint32_t flags;
        mxcfb_alt_buffer_data alt_buffer_data{};
    };
    struct mxcfb_update_marker_data {
        uint32_t update_marker;
        uint32_t collision_test;
    };
    static constexpr unsigned long MXCFB_SEND_UPDATE = 1078478382UL; // 0x4048462e
    static constexpr unsigned long MXCFB_WAIT_FOR_UPDATE_COMPLETE = 3221767727UL;
    static constexpr int TEMP_USE_AMBIENT = 4096;
    int fd = -1;
    uint32_t marker = 0;
    uint32_t okCount = 0;
    uint32_t failCount = 0;
    uint32_t failWait = 0;
};

class ShmImageProvider : public QQuickImageProvider {
public:
    ShmImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    void setBuffers(uint8_t *buf0, uint8_t *buf1, const ShmHeader *hdr) {
        m_buf0 = buf0; m_buf1 = buf1; m_hdr = hdr;
    }
    void setFormat(QImage::Format fmt) { m_format = fmt; }
    QImage requestImage(const QString &, QSize *size, const QSize &) override {
        if (!m_hdr || !m_buf0) return {};
        const uint8_t *src = (m_hdr->active_buffer == 0) ? m_buf0 : m_buf1;
        QImage img(src, static_cast<int>(m_hdr->width), static_cast<int>(m_hdr->height),
                   static_cast<int>(m_hdr->stride), m_format);
        if (size) *size = img.size();
        QImage copy = img.copy(); // detach from shm
        // Debug: occasional log to trace refresh consumption
        static int reqCount = 0;
        if (++reqCount % 50 == 0) {
            qInfo() << "[SHM-REQ]" << "req" << reqCount
                    << "gen" << m_hdr->gen_counter
                    << "buf" << m_hdr->active_buffer;
        }
        return copy;
    }
private:
    const ShmHeader *m_hdr = nullptr;
    uint8_t *m_buf0 = nullptr;
    uint8_t *m_buf1 = nullptr;
    QImage::Format m_format = QImage::Format_ARGB32;
};

class ShmWatcher : public QObject {
    Q_OBJECT
    Q_PROPERTY(uint gen READ gen NOTIFY genChanged)
public:
    explicit ShmWatcher(QObject *parent = nullptr) : QObject(parent) {
        m_timer.setInterval(100);  // Poll more frequently for better scroll detection
        connect(&m_timer, &QTimer::timeout, this, &ShmWatcher::poll);
        
        // Idle cleanup timer: triggers GC16 full refresh after no changes for a while
        m_idleTimer.setSingleShot(true);
        m_idleTimer.setInterval(500);  // 500ms after last change
        connect(&m_idleTimer, &QTimer::timeout, this, &ShmWatcher::onIdleCleanup);
    }
    void setUpdateTrigger(FbUpdateTrigger *t) { m_updateTrigger = t; }
    uint gen() const { return m_gen; }
    int width() const { return m_hdr ? static_cast<int>(m_hdr->width) : 0; }
    int height() const { return m_hdr ? static_cast<int>(m_hdr->height) : 0; }
    bool init(const QString &path, ShmImageProvider *provider) {
        int fd = ::open(path.toUtf8().constData(), O_RDONLY);
        if (fd < 0) {
            qWarning() << "open failed" << strerror(errno);
            return false;
        }
        struct stat st{};
        if (::fstat(fd, &st) != 0) {
            qWarning() << "fstat failed";
            ::close(fd);
            return false;
        }
        void *base = mmap(nullptr, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
        ::close(fd);
        if (base == MAP_FAILED) {
            qWarning() << "mmap failed" << strerror(errno);
            return false;
        }
        m_base.reset(base, st.st_size);
        m_hdr = reinterpret_cast<const ShmHeader *>(base);
        if (m_hdr->magic != SHM_MAGIC || m_hdr->version != SHM_VERSION) {
            qWarning() << "bad magic/version";
            return false;
        }
        qInfo() << "[SHM] init ok"
                << "w" << m_hdr->width << "h" << m_hdr->height
                << "stride" << m_hdr->stride
                << "format" << m_hdr->format
                << "gen" << m_hdr->gen_counter;
        const size_t headerBytes = sizeof(ShmHeader);
        m_buf0 = reinterpret_cast<uint8_t *>(base) + headerBytes;
        m_buf1 = m_buf0 + static_cast<size_t>(m_hdr->stride) * m_hdr->height;
        provider->setBuffers(m_buf0, m_buf1, m_hdr);
        QImage::Format fmt = QImage::Format_ARGB32;
        if (m_hdr->format == SHM_FMT_RGB565) fmt = QImage::Format_RGB16;
        provider->setFormat(fmt);
        m_format = fmt;
        
        // Allocate previous frame buffer for diff detection
        size_t frameSize = static_cast<size_t>(m_hdr->stride) * m_hdr->height;
        m_prevFrame.resize(frameSize);
        const uint8_t *currentBuf = (m_hdr->active_buffer == 0) ? m_buf0 : m_buf1;
        std::memcpy(m_prevFrame.data(), currentBuf, frameSize);
        
        m_gen = m_hdr->gen_counter;
        emit genChanged();
        
        // Force one full GC16 refresh on startup
        if (m_updateTrigger) {
            triggerWithRepeat(0, 0,
                              static_cast<int>(m_hdr->width),
                              static_cast<int>(m_hdr->height),
                              FbUpdateTrigger::WAVE_GC16,
                              FbUpdateTrigger::MODE_FULL);
            qInfo() << "[SHM] Initial full GC16 refresh triggered";
        }
        m_timer.start();
        return true;
    }
signals:
    void genChanged();
private slots:
    void poll() {
        if (!m_hdr) return;
        if (m_hdr->gen_counter != m_gen) {
            m_gen = m_hdr->gen_counter;
            
            // Detect changed region and apply smart refresh
            smartRefresh();
            
            emit genChanged();
        }
    }
    
    void onIdleCleanup() {
        if (!m_hdr || !m_updateTrigger) return;
        // Only cleanup if we had partial updates that may have left ghosting
        if (m_partialCount >= kMaxPartialBeforeCleanup || m_a2Count >= kMaxA2BeforeCleanup) {
            qInfo() << "[SMART] Idle cleanup: GC16 full refresh"
                    << "partialCount" << m_partialCount << "a2Count" << m_a2Count;
            triggerWithRepeat(0, 0,
                              static_cast<int>(m_hdr->width),
                              static_cast<int>(m_hdr->height),
                              FbUpdateTrigger::WAVE_GC16,
                              FbUpdateTrigger::MODE_FULL);
            m_partialCount = 0;
            m_a2Count = 0;
            m_lastChangeTime = 0;
        }
    }
    
private:
    // Detect changed region between previous and current frame
    struct ChangedRegion {
        int x, y, w, h;
        int changedPixels;
        double changeRatio;  // Percentage of total pixels
    };
    
    ChangedRegion detectChangedRegion() {
        ChangedRegion result{0, 0, 0, 0, 0, 0.0};
        if (!m_hdr || m_prevFrame.empty()) return result;
        
        const uint8_t *currentBuf = (m_hdr->active_buffer == 0) ? m_buf0 : m_buf1;
        const int width = static_cast<int>(m_hdr->width);
        const int height = static_cast<int>(m_hdr->height);
        const int stride = static_cast<int>(m_hdr->stride);
        const int bytesPerPixel = (m_format == QImage::Format_RGB16) ? 2 : 4;
        
        int minX = width, maxX = 0, minY = height, maxY = 0;
        int changedPixels = 0;
        
        // Sample every 4th pixel for speed (good enough for region detection)
        const int step = 4;
        for (int y = 0; y < height; y += step) {
            const uint8_t *prevRow = m_prevFrame.data() + y * stride;
            const uint8_t *currRow = currentBuf + y * stride;
            for (int x = 0; x < width; x += step) {
                int offset = x * bytesPerPixel;
                bool changed = false;
                // Compare pixels with threshold to ignore minor noise
                for (int b = 0; b < bytesPerPixel; ++b) {
                    if (std::abs(prevRow[offset + b] - currRow[offset + b]) > 8) {
                        changed = true;
                        break;
                    }
                }
                if (changed) {
                    changedPixels++;
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
            }
        }
        
        // Update previous frame
        size_t frameSize = static_cast<size_t>(stride) * height;
        std::memcpy(m_prevFrame.data(), currentBuf, frameSize);
        
        if (changedPixels == 0) {
            return result;
        }
        
        // Expand region slightly to account for sampling
        minX = std::max(0, minX - step);
        minY = std::max(0, minY - step);
        maxX = std::min(width - 1, maxX + step);
        maxY = std::min(height - 1, maxY + step);
        
        result.x = minX;
        result.y = minY;
        result.w = maxX - minX + 1;
        result.h = maxY - minY + 1;
        result.changedPixels = changedPixels * step * step;  // Approximate actual count
        
        int totalSamples = (width / step) * (height / step);
        result.changeRatio = static_cast<double>(changedPixels) / totalSamples;
        
        return result;
    }
    
    void smartRefresh() {
        if (!m_updateTrigger || !m_hdr) return;
        
        const int width = static_cast<int>(m_hdr->width);
        const int height = static_cast<int>(m_hdr->height);
        
        ChangedRegion region = detectChangedRegion();
        
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 timeSinceLast = (m_lastChangeTime > 0) ? (now - m_lastChangeTime) : 1000;
        m_lastChangeTime = now;
        
        // Reset idle timer on each change
        m_idleTimer.start();
        
        if (region.changedPixels == 0) {
            qInfo() << "[SMART] No visible change detected, skip refresh";
            return;
        }
        
        qInfo() << "[SMART] Changed region:" << region.x << region.y << region.w << region.h
                << "ratio" << QString::number(region.changeRatio * 100, 'f', 1) << "%"
                << "timeSinceLast" << timeSinceLast << "ms";
        
        // Determine refresh strategy
        bool isScrolling = (timeSinceLast < 200) && (region.changeRatio > 0.3);
        bool isLargeChange = (region.changeRatio > 0.5);
        bool isSmallChange = (region.changeRatio < 0.15);
        bool needsFullCleanup = (m_partialCount >= kMaxPartialBeforeCleanup) || 
                                 (m_a2Count >= kMaxA2BeforeCleanup);
        
        if (needsFullCleanup) {
            // Too many partial updates, do a full GC16 cleanup
            qInfo() << "[SMART] Cleanup threshold reached, full GC16 refresh"
                    << "partialCount" << m_partialCount << "a2Count" << m_a2Count;
            triggerWithRepeat(0, 0, width, height, FbUpdateTrigger::WAVE_GC16, FbUpdateTrigger::MODE_FULL);
            m_partialCount = 0;
            m_a2Count = 0;
        } else if (isScrolling || isLargeChange) {
            // Scrolling or large change: use A2 for speed, accept some ghosting
            qInfo() << "[SMART] Scrolling/large change: A2 partial refresh";
            triggerWithRepeat(region.x, region.y, region.w, region.h,
                              FbUpdateTrigger::WAVE_A2, FbUpdateTrigger::MODE_PARTIAL);
            m_a2Count++;
        } else if (isSmallChange) {
            // Small change: use GC16 partial for quality
            qInfo() << "[SMART] Small change: GC16 partial refresh";
            triggerWithRepeat(region.x, region.y, region.w, region.h,
                              FbUpdateTrigger::WAVE_GC16, FbUpdateTrigger::MODE_PARTIAL);
            m_partialCount++;
        } else {
            // Medium change: use GC16 on the changed region
            qInfo() << "[SMART] Medium change: GC16 partial refresh";
            triggerWithRepeat(region.x, region.y, region.w, region.h,
                              FbUpdateTrigger::WAVE_GC16, FbUpdateTrigger::MODE_PARTIAL);
            m_partialCount++;
        }
    }
    
    struct MmapDeleter {
        void operator()(void *p, size_t sz) const {
            if (p) munmap(p, sz);
        }
    };
    struct MmapPtr {
        MmapPtr() : ptr(nullptr), sz(0) {}
        MmapPtr(void *p, size_t s) : ptr(p), sz(s) {}
        void reset(void *p, size_t s) { ptr = p; sz = s; }
        ~MmapPtr() { if (ptr) munmap(ptr, sz); }
        void *ptr; size_t sz;
    };
    MmapPtr m_base;
    const ShmHeader *m_hdr = nullptr;
    uint8_t *m_buf0 = nullptr;
    uint8_t *m_buf1 = nullptr;
    uint m_gen = 0;
    QTimer m_timer;
    QTimer m_idleTimer;
    FbUpdateTrigger *m_updateTrigger = nullptr;
    
    // Previous frame for diff detection
    std::vector<uint8_t> m_prevFrame;
    QImage::Format m_format = QImage::Format_ARGB32;
    
    // Smart refresh counters
    int m_partialCount = 0;
    int m_a2Count = 0;
    qint64 m_lastChangeTime = 0;
    
    // Thresholds
    static constexpr int kMaxPartialBeforeCleanup = 10;  // Full refresh after N partial updates
    static constexpr int kMaxA2BeforeCleanup = 10;       // Full refresh after N A2 updates

    // Trigger refresh and force a repeat 200ms later (same region/wave/mode)
    void triggerWithRepeat(int x, int y, int w, int h,
                           FbUpdateTrigger::WaveformMode wave,
                           FbUpdateTrigger::UpdateMode mode) {
        if (!m_updateTrigger) return;
        m_updateTrigger->triggerRegion(x, y, w, h, wave, mode);
        QTimer::singleShot(200, this, [this, x, y, w, h, wave, mode]() {
            if (m_updateTrigger) {
                m_updateTrigger->triggerRegion(x, y, w, h, wave, mode);
            }
        });
    }
};

class TouchSender : public QObject {
    Q_OBJECT
public:
    explicit TouchSender(QObject *parent = nullptr) : QObject(parent) {
        target = QHostAddress(QStringLiteral("127.0.0.1"));
        socket.connectToHost(target, 45454);
        if (auto screen = QGuiApplication::primaryScreen()) {
            const QSize s = screen->size();
            windowHeight = s.height();
            windowWidth = s.width();
        }
        // listen for book/non-book state updates on UDP 45456 (1 byte: 1=book,0=non-book)
        if (stateSocket.bind(QHostAddress::LocalHost, 45456,
                             QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            qInfo() << "[GESTURE] state socket bound, default isBookPage" << isBookPage;
            connect(&stateSocket, &QUdpSocket::readyRead, this, [this]() {
                while (stateSocket.hasPendingDatagrams()) {
                    QByteArray d;
                    d.resize(int(stateSocket.pendingDatagramSize()));
                    stateSocket.readDatagram(d.data(), d.size());
                    if (!d.isEmpty()) {
                        bool newState = d.at(0) != 0;
                        if (newState != isBookPage) {
                            isBookPage = newState;
                            qInfo() << "[GESTURE] isBookPage set to" << isBookPage;
                        }
                        hasState = true;
                        stateTimer.stop();
                    }
                }
            });
        } else {
            qWarning() << "[GESTURE] failed to bind state socket";
        }
        // periodic request until we have a state
        stateTimer.setSingleShot(false);
        stateTimer.setInterval(2000);
        connect(&stateTimer, &QTimer::timeout, this, [this]() {
            if (hasState) { stateTimer.stop(); return; }
            QByteArray req(1, '\xFF'); // arbitrary content to signal request
            const qint64 sent = stateRequestSocket.writeDatagram(req, target, 45457);
            qInfo() << "[GESTURE] requesting isBookPage state bytes" << sent;
        });
        stateTimer.start();
        // fire an immediate request so we don't wait for the first interval
        QMetaObject::invokeMethod(this, [this]() {
            if (!hasState) {
                QByteArray req(1, '\xFF');
                const qint64 sent = stateRequestSocket.writeDatagram(req, target, 45457);
                qInfo() << "[GESTURE] requesting isBookPage state bytes" << sent;
            }
        }, Qt::QueuedConnection);
        // menu timeout
        menuTimer.setSingleShot(true);
        menuTimer.setInterval(5000);
        connect(&menuTimer, &QTimer::timeout, this, [this]() {
            menuActive = false;
            menuTap = false;
            qInfo() << "[MENU] timed out ts" << QDateTime::currentMSecsSinceEpoch();
        });
    }

    void setWindowHeight(qreal h) { windowHeight = h; }
    void setWindowWidth(qreal w) { windowWidth = w; }
    bool eventFilter(QObject *obj, QEvent *event) override {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(event);
            handlePoint(me->position(), event->type());
            return true; // always handle here
        }
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd: {
            auto *te = static_cast<QTouchEvent *>(event);
            const auto points = te->points();
            if (!points.isEmpty()) {
                handlePoint(points.first().position(), event->type());
            }
            return true;
        }
        default:
            break;
        }
        return QObject::eventFilter(obj, event);
    }

signals:
    void exitGestureDetected();
    void fullRefreshRequested();

private:
    enum SpecialType : quint8 {
        EXIT = 101,         // 保留枚举值但不再触发
        BACK = 102,
        NEXT_PAGE = 103,
        PREV_PAGE = 104,
        SCROLL_UP = 105,
        SCROLL_DOWN = 106,
        MENU_OPEN = 107,
        MENU_FONT_TOGGLE = 108,
        MENU_FONT_UP = 109,
        MENU_FONT_DOWN = 110,
        MENU_UA = 111,      // 以前的 MENU_THEME，现用于 UA 切换
        MENU_SERVICE = 112,
        FULL_REFRESH = 113  // 新增：全屏刷新
    };

    struct Payload { quint8 type; quint8 pad[3]; float x; float y; };

    void sendPayload(const Payload &p) {
        socket.writeDatagram(reinterpret_cast<const char*>(&p), sizeof(p), target, 45454);
        const qint64 ts = QDateTime::currentMSecsSinceEpoch();
        qInfo() << "[TOUCH-SEND]" << "ts" << ts << "type" << p.type << "pos" << p.x << p.y;
    }

    void sendCommand(SpecialType t) {
        Payload p{}; p.type = static_cast<quint8>(t); p.x = 0; p.y = 0; sendPayload(p);
    }

    void logAction(const char* action, const QPointF& pos, qreal dx, qreal dy) {
        const qint64 ts = QDateTime::currentMSecsSinceEpoch();
        qInfo() << "[GESTURE]" << action
                << "ts" << ts
                << "start" << startPos << "end" << pos
                << "dx" << dx << "dy" << dy
                << "isBookPage" << isBookPage
                << "startInBottom" << candidateExit;
    }

public:
    qreal getWindowWidth() const { return windowWidth; }
    qreal getWindowHeight() const { return windowHeight; }

    void triggerFullRefresh() {
        emit fullRefreshRequested();
    }
private:
    void flushBuffered() {
        for (const auto &p : buffered) sendPayload(p);
        buffered.clear();
    }

    void handlePoint(const QPointF &pos, QEvent::Type type) {
        Payload p{};
        p.type = (type == QEvent::MouseButtonPress || type == QEvent::TouchBegin) ? 1 :
                 (type == QEvent::MouseButtonRelease || type == QEvent::TouchEnd) ? 3 : 2;
        p.x = pos.x();
        p.y = pos.y();

        // 额外的原始触摸日志，方便排查触摸是否送达 viewer
        qInfo() << "[TOUCH-RAW]" << "ts" << QDateTime::currentMSecsSinceEpoch()
                << ((p.type==1)?"press":(p.type==3)?"release":"move")
                << "pos" << pos << "type" << p.type << "isBook" << isBookPage;

        if (p.type == 1) {
            inGesture = true;
            candidateExit = (pos.y() >= windowHeight - bottomMargin);
            swallowThisGesture = false;
            buffered.clear();
            startPos = pos;
            elapsed.restart();
            buffered.push_back(p); // always buffer; decide on release
            // If menu active, intercept top taps for menu actions
            if (menuActive && pos.y() <= menuTopHeight && pos.x() <= windowWidth - topRightReserve) {
                menuTap = true;
                swallowThisGesture = true;
                return;
            }
            return;
        }

        if (!inGesture) { return; }

        // Exit / Back detection
        if (!swallowThisGesture) {
            buffered.push_back(p);
            qreal dy = startPos.y() - pos.y();
            qreal dx = pos.x() - startPos.x();
            qreal absDx = qAbs(dx);
            qreal absDy = qAbs(dy);
            if (candidateExit && dy >= exitDelta && absDx <= exitHorizTol) {
                logAction("FULL_REFRESH", pos, dx, -dy);
                swallowThisGesture = true; buffered.clear(); triggerFullRefresh();
            } else if (!candidateExit && startPos.x() <= leftEdge && dx >= backDelta && absDy <= backVertTol) {
                logAction("BACK", pos, dx, -dy);
                swallowThisGesture = true; buffered.clear(); sendCommand(BACK);
            } else if (startPos.y() <= topMargin && -dy >= menuDelta && absDx <= topHorizTol
                       && startPos.x() <= windowWidth - topRightReserve) {
                logAction("MENU_OPEN", pos, dx, -dy);
                swallowThisGesture = true; buffered.clear(); sendCommand(MENU_OPEN);
                menuActive = true;
                menuTap = false;
                menuTimer.start();
            } else if (startPos.y() <= topMargin) {
                logAction("MENU_CANDIDATE_NO_TRIGGER", pos, dx, -dy);
            }
        }

        // Page / scroll gestures
        if (!swallowThisGesture) {
            qreal dy = pos.y() - startPos.y();
            qreal dx = pos.x() - startPos.x();
            qreal absDy = qAbs(dy);
            bool inMiddle = (startPos.x() >= leftEdge && startPos.x() <= windowWidth - rightEdge);
            if (isBookPage && inMiddle) {
                const bool startValid = startPos.y() > 60.0;
                if (startValid && dx <= -pageSwipeMin && absDy <= pageVertTol) { logAction("NEXT_PAGE", pos, dx, dy); swallowThisGesture = true; buffered.clear(); sendCommand(NEXT_PAGE); }
                else if (startValid && dx >= pageSwipeMin && absDy <= pageVertTol) { logAction("PREV_PAGE", pos, dx, dy); swallowThisGesture = true; buffered.clear(); sendCommand(PREV_PAGE); }
            } else if (!isBookPage && inMiddle) {
                // swapped: drag up -> scroll down; drag down -> scroll up (1 page step handled frontend)
                if (dy <= -scrollMin) { logAction("SCROLL_DOWN", pos, dx, dy); swallowThisGesture = true; buffered.clear(); sendCommand(SCROLL_DOWN); }
                else if (dy >= scrollMin) { logAction("SCROLL_UP", pos, dx, dy); swallowThisGesture = true; buffered.clear(); sendCommand(SCROLL_UP); }
            }
        }

            if (menuActive && menuTap && p.type == 3 && pos.x() <= windowWidth - topRightReserve) {
            // menu tap release: map x to action
            const qreal colWidth = windowWidth / 5.0;
            int idx = static_cast<int>(pos.x() / colWidth);
            if (idx < 0) idx = 0;
            if (idx > 4) idx = 4;
            switch (idx) {
            case 0: sendCommand(MENU_FONT_TOGGLE); logAction("MENU_FONT_TOGGLE", pos, 0, 0); break;
            case 1: sendCommand(MENU_SERVICE); logAction("MENU_SERVICE", pos, 0, 0); break;
            case 2: sendCommand(MENU_FONT_UP); logAction("MENU_FONT_UP", pos, 0, 0); break;
            case 3: sendCommand(MENU_FONT_DOWN); logAction("MENU_FONT_DOWN", pos, 0, 0); break;
            case 4: sendCommand(MENU_UA); logAction("MENU_UA", pos, 0, 0); break;
            }
            menuTap = false;
            menuTimer.start();
            inGesture = false;
            candidateExit = false;
            swallowThisGesture = false;
            buffered.clear();
            return;
        }

        if (p.type == 3) {
            if (!swallowThisGesture) {
                qreal dx = pos.x() - startPos.x();
                qreal dy = pos.y() - startPos.y();
                qreal absDx = qAbs(dx);
                qreal absDy = qAbs(dy);
                qint64 ms = elapsed.elapsed();
            if (menuActive && !menuTap && pos.x() <= windowWidth - topRightReserve) {
                // ignore clicks during menu mode outside the top area
                logAction("MENU_ACTIVE_SWALLOW", pos, dx, dy);
                menuActive = false;
                menuTap = false;
                menuTimer.stop();
                    inGesture = false;
                    candidateExit = false;
                    swallowThisGesture = false;
                    buffered.clear();
                    return;
                }
                bool isTap = (ms < tapMs && absDx < tapThreshold && absDy < tapThreshold);
                bool isLong = (ms >= tapMs && absDx < tapThreshold && absDy < tapThreshold);
                if (isBookPage) {
                    if (isTap) {
                        logAction("TAP_NEXT_PAGE", pos, dx, dy);
                        sendCommand(NEXT_PAGE);
                    } else if (isLong) {
                        logAction("LONG_PRESS_PASS", pos, dx, dy);
                        flushBuffered(); // long press passes through
                    } else {
                        qInfo() << "[GESTURE] discard book gesture"
                                << "start" << startPos << "end" << pos
                                << "tap=false long=false"
                                << "absDx" << absDx << "absDy" << absDy << "ms" << ms
                                << "isBookPage" << isBookPage
                                << "startInBottom" << candidateExit;
                    }
                } else { // non-book
                    if (isTap || isLong) {
                        logAction(isTap ? "TAP_PASS" : "LONG_PRESS_PASS", pos, dx, dy);
                        flushBuffered(); // allow click/long-press
                    } else {
                        qInfo() << "[GESTURE] discard non-book gesture"
                                << "start" << startPos << "end" << pos
                                << "tap=false long=false"
                                << "absDx" << absDx << "absDy" << absDy << "ms" << ms
                                << "isBookPage" << isBookPage
                                << "startInBottom" << candidateExit;
                    }
                }
            }
            inGesture = false;
            candidateExit = false;
            swallowThisGesture = false;
            buffered.clear();
        }
    }

    QUdpSocket socket;
    QHostAddress target;
    QUdpSocket stateSocket;
    bool inGesture = false;
    bool candidateExit = false;
    bool swallowThisGesture = false;
    bool menuActive = false;
    bool menuTap = false;
    QPointF startPos;
    QVector<Payload> buffered;
    qreal windowHeight = 1696.0;
    qreal windowWidth = 1404.0;
    QElapsedTimer elapsed;
    bool isBookPage = false; // updated via UDP 45456
    bool hasState = false;

    // thresholds
    const qreal bottomMargin = 100.0;
    const qreal exitDelta = 100.0;
    const qreal exitHorizTol = 200.0;
    const qreal leftEdge = 40.0;
    const qreal rightEdge = 40.0;
    const qreal topMargin = 130.0;      // shift menu trigger down by 50px (was 80)
    const qreal backDelta = 200.0;
    const qreal backVertTol = 250.0;
    const qreal topHorizTol = 150.0;
    const qreal menuDelta = 100.0;
    const qreal menuTopHeight = 170.0;  // shift clickable area down by 50px (was 120)
    const qreal topRightReserve = 0.0;  // no right-side reserve, full width
    const qreal pageSwipeMin = 100.0;
    const qreal pageVertTol = 250.0;
    const qreal scrollMin = 100.0;
    const qreal tapThreshold = 100.0;
    const qint64 tapMs = 500;
    QUdpSocket stateRequestSocket;
    QTimer stateTimer;
    QTimer menuTimer;
};

class ExitHelper : public QObject {
    Q_OBJECT
public:
    explicit ExitHelper(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void exitViewer() {
        QCoreApplication::exit(0);
    }
};

// Power key handler: suspend device, keep frontend/browser untouched.
class PowerKeyFilter : public QObject {
public:
    explicit PowerKeyFilter(QObject *parent = nullptr) : QObject(parent) {}
protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            auto *ke = static_cast<QKeyEvent *>(event);
            const int key = ke->key();
            if (key == Qt::Key_PowerOff || key == Qt::Key_PowerDown) {
                QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("suspend")});
                return true;
            }
        }
        return QObject::eventFilter(obj, event);
    }
};

int main(int argc, char *argv[]) {
    // Favor software rendering pipeline so LD_PRELOAD qtfb-shim can intercept fb writes.
    // Works with both linuxfb and epaper platforms - shim intercepts framebuffer ioctl calls.
    qputenv("QSG_RENDER_LOOP", QByteArrayLiteral("basic"));
    qputenv("QSG_RHI_BACKEND", QByteArrayLiteral("software"));
    qputenv("QT_QUICK_BACKEND", QByteArrayLiteral("software"));

    QCoreApplication::setApplicationVersion(QString::fromLatin1(kViewerVersion));
    QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTouchEvents);
    QCoreApplication::setAttribute(Qt::AA_SynthesizeTouchForUnhandledMouseEvents);
    QGuiApplication app(argc, argv);
    qInfo() << "[VIEWER] version" << QCoreApplication::applicationVersion();
    TouchSender sender;
    app.installEventFilter(&sender);
    PowerKeyFilter powerFilter;
    app.installEventFilter(&powerFilter);
    QQmlApplicationEngine engine;

    auto provider = new ShmImageProvider();
    engine.addImageProvider("shmframe", provider);

    FbUpdateTrigger fbTrigger;
    ShmWatcher watcher;
    watcher.setUpdateTrigger(&fbTrigger);
    if (!watcher.init("/dev/shm/weread_frame", provider)) {
        qWarning() << "Failed to init shm watcher";
    }
    engine.rootContext()->setContextProperty("shmWatcher", &watcher);

    ExitHelper exitHelper;
    engine.rootContext()->setContextProperty("exitHelper", &exitHelper);
    QObject::connect(&sender, &TouchSender::exitGestureDetected,
                     &exitHelper, &ExitHelper::exitViewer);
    QObject::connect(&sender, &TouchSender::fullRefreshRequested, [&fbTrigger, &watcher, &sender](){
        int w = watcher.width();
        int h = watcher.height();
        if (w <= 0 || h <= 0) {
            w = static_cast<int>(sender.getWindowWidth());
            h = static_cast<int>(sender.getWindowHeight());
        }
        if (w <= 0 || h <= 0) {
            w = 1404;
            h = 1696;
        }
        qInfo() << "[GESTURE] full refresh trigger" << w << h;
        fbTrigger.triggerFullCleanup(w, h);
    });

    using namespace Qt::StringLiterals;
    QString qmlPath = QLatin1String("/home/root/weread/main.qml");
    if (!QFile::exists(qmlPath)) {
        qmlPath = QCoreApplication::applicationDirPath() + "/main.qml";
    }
    qInfo() << "[QML] loading" << qmlPath;
    const QUrl url = QUrl::fromLocalFile(qmlPath);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    engine.load(url);
    return app.exec();
}

#include "main.moc"
