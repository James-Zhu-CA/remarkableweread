#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <QTimer>
#include <QDebug>
#include <QScreen>
#include <QThread>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <cstring>

class ColorWidget : public QWidget {
public:
    ColorWidget(QColor color) : m_color(color) {
        setFixedSize(1696, 954);  // Expected Paper Pro Move resolution
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), m_color);

        // Draw some text
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 48, QFont::Bold));
        painter.drawText(rect(), Qt::AlignCenter, "DRM Refresh Test");
    }

private:
    QColor m_color;
};

void queryDrmModes() {
    qInfo() << "\n=== Querying DRM Display Modes ===";

    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        qWarning() << "Failed to open /dev/dri/card0:" << strerror(errno);
        return;
    }

    drmModeRes* resources = drmModeGetResources(fd);
    if (!resources) {
        qWarning() << "Failed to get DRM resources";
        close(fd);
        return;
    }

    qInfo() << "Found" << resources->count_connectors << "connectors,"
            << resources->count_crtcs << "CRTCs,"
            << resources->count_encoders << "encoders";

    // Check each connector
    for (int i = 0; i < resources->count_connectors; i++) {
        drmModeConnector* conn = drmModeGetConnector(fd, resources->connectors[i]);
        if (!conn) continue;

        qInfo() << "\nConnector" << conn->connector_id << ":"
                << "Type:" << conn->connector_type
                << "Status:" << (conn->connection == DRM_MODE_CONNECTED ? "Connected" : "Disconnected");

        if (conn->connection == DRM_MODE_CONNECTED) {
            qInfo() << "  Available modes:" << conn->count_modes;
            for (int j = 0; j < conn->count_modes; j++) {
                drmModeModeInfo* mode = &conn->modes[j];
                qInfo() << "    Mode" << j << ":"
                        << mode->hdisplay << "x" << mode->vdisplay
                        << "@" << mode->vrefresh << "Hz"
                        << "name:" << mode->name;
            }

            // Check current encoder/CRTC
            if (conn->encoder_id) {
                drmModeEncoder* enc = drmModeGetEncoder(fd, conn->encoder_id);
                if (enc && enc->crtc_id) {
                    drmModeCrtc* crtc = drmModeGetCrtc(fd, enc->crtc_id);
                    if (crtc) {
                        qInfo() << "  Current CRTC mode:" << crtc->width << "x" << crtc->height;
                        drmModeFreeCrtc(crtc);
                    }
                    drmModeFreeEncoder(enc);
                }
            }
        }

        drmModeFreeConnector(conn);
    }

    drmModeFreeResources(resources);
    close(fd);
}

bool testDrmRefresh(uint32_t cfg_width, uint32_t cfg_height, uint32_t cfg_x, uint32_t cfg_y,
                    uint32_t mode_y, uint32_t mode_width, uint32_t mode_height,
                    uint32_t mode_x1, uint32_t mode_x2, const QString& description) {
    qInfo() << "\n=== Testing DRM Refresh:" << description << "===";

    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        qWarning() << "Failed to open /dev/dri/card0";
        return false;
    }

    static uint32_t marker = 1;

    // CONFIG
    drm_eink_config_params cfg{};
    cfg.width = cfg_width;
    cfg.height = cfg_height;
    cfg.x = cfg_x;
    cfg.y = cfg_y;
    cfg.waveform = 0;
    cfg.flags = 0;

    qInfo() << "  CONFIG:" << cfg_width << "x" << cfg_height << "at (" << cfg_x << "," << cfg_y << ")";
    if (ioctl(fd, DRM_EINK_CONFIG_REFRESH, &cfg) < 0) {
        qWarning() << "  CONFIG failed:" << strerror(errno);
        close(fd);
        return false;
    }
    qInfo() << "  CONFIG succeeded";

    // MODE_QUEUE
    drm_eink_mode_params mode{};
    mode.reserved1 = 0;
    mode.y = mode_y;
    mode.width = mode_width;
    mode.height = mode_height;
    mode.x1 = mode_x1;
    mode.x2 = mode_x2;
    mode.slot = marker;

    qInfo() << "  MODE_QUEUE: y=" << mode_y << "width=" << mode_width << "height=" << mode_height
            << "x1=" << mode_x1 << "x2=" << mode_x2 << "slot=" << marker;
    if (ioctl(fd, DRM_EINK_MODE_QUEUE, &mode) < 0) {
        qWarning() << "  MODE_QUEUE failed:" << strerror(errno);
        close(fd);
        return false;
    }
    qInfo() << "  MODE_QUEUE succeeded";

    // TRIGGER
    drm_eink_refresh_params refresh{};
    refresh.marker = marker;
    refresh.flags = 0;

    qInfo() << "  TRIGGER: marker=" << marker;
    if (ioctl(fd, DRM_EINK_TRIGGER_REFRESH, &refresh) < 0) {
        qWarning() << "  TRIGGER failed:" << strerror(errno);
        close(fd);
        return false;
    }
    qInfo() << "  TRIGGER succeeded";

    marker++;
    if (marker > 16) marker = 1;

    close(fd);
    return true;
}

int main(int argc, char* argv[]) {
    // Set Qt environment - use eglfs like the main app
    qputenv("QT_QPA_PLATFORM", "eglfs");
    qputenv("QT_QPA_EGLFS_ALWAYS_SET_MODE", "1");
    qputenv("QT_LOGGING_RULES", "qt.qpa.*=true");

    QApplication app(argc, argv);

    qInfo() << "=== Display Diagnostic Tool ===";
    qInfo() << "Qt detected platform:" << QGuiApplication::platformName();
    qInfo() << "Primary screen size:" << QGuiApplication::primaryScreen()->size();
    qInfo() << "Primary screen geometry:" << QGuiApplication::primaryScreen()->geometry();

    // Query DRM modes
    queryDrmModes();

    // Create colored widget
    ColorWidget* widget = new ColorWidget(Qt::blue);
    widget->show();

    qInfo() << "\n=== Widget Created ===";
    qInfo() << "Widget size:" << widget->size();
    qInfo() << "Widget geometry:" << widget->geometry();

    // Wait for rendering
    QTimer::singleShot(1000, &app, [&app]() {
        qInfo() << "\n=== Starting Refresh Tests (after 1s delay) ===";

        // Test 1: xochitl's exact params
        testDrmRefresh(
            1700, 365, 16, 0,      // CONFIG: width, height, x, y
            365, 1700, 730, 16, 16, // MODE_QUEUE: y, width, height, x1, x2
            "xochitl params (original)"
        );

        QThread::sleep(2);

        // Test 2: Try adapted params for 365-wide display
        testDrmRefresh(
            365, 1700, 0, 0,       // CONFIG: swap width/height, no offset
            0, 365, 1700, 0, 0,    // MODE_QUEUE: cover full height
            "Qt-adapted params (365×1700)"
        );

        QThread::sleep(2);

        // Test 3: Try full screen params assuming 1696×954
        testDrmRefresh(
            1696, 954, 0, 0,       // CONFIG: full screen
            0, 1696, 954, 0, 0,    // MODE_QUEUE: full screen
            "Full screen params (1696×954)"
        );

        QThread::sleep(2);

        qInfo() << "\n=== Tests Complete - Exiting in 3s ===";
        QTimer::singleShot(3000, &app, &QApplication::quit);
    });

    return app.exec();
}
