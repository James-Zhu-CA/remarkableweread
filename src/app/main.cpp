#include "catalog_widget.h"
#include <QAbstractSocket>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QNetworkCookie>
#include <QProcess>
#include <QPushButton>
#include <QRect>
#include <QStandardPaths>
#include <QString>
#include <QTcpSocket>
#include <QTimer>
#include <QTouchEvent>
#include <QUdpSocket>
#include <QUrl>
#include <QWebEngineCookieStore>
#include <QWebEngineHistory>
#include <QWebEngineNavigationRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>
#include <QWheelEvent>
#include <QtGlobal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "common.h"
#include "gesture_filter.h"
#include "weread_browser.h"

// g_logLevel 定义（声明在 common.h）
LogLevel g_logLevel = LogLevel::Warning;
static QtMessageHandler g_prevHandler = nullptr;
static void initLogLevelFromEnv() {
  const QByteArray levelEnv = qgetenv("WEREAD_LOG_LEVEL");
  if (levelEnv.isEmpty()) {
    g_logLevel = LogLevel::Warning;
    return;
  }
  const QString level = QString::fromLatin1(levelEnv).trimmed().toLower();
  if (level == QStringLiteral("info") || level == QStringLiteral("debug")) {
    g_logLevel = LogLevel::Info;
  } else if (level == QStringLiteral("error") ||
             level == QStringLiteral("err")) {
    g_logLevel = LogLevel::Error;
  } else {
    g_logLevel = LogLevel::Warning;
  }
}
// logLevelAtLeast 已在 common.h 中定义为 inline 函数
static void filteredMessageHandler(QtMsgType type,
                                   const QMessageLogContext &ctx,
                                   const QString &msg) {
  switch (type) {
  case QtDebugMsg:
  case QtInfoMsg:
    if (!logLevelAtLeast(LogLevel::Info))
      return;
    break;
  case QtWarningMsg:
    if (!logLevelAtLeast(LogLevel::Warning))
      return;
    break;
  case QtCriticalMsg:
    if (!logLevelAtLeast(LogLevel::Error))
      return;
    break;
  case QtFatalMsg:
    break;
  }
  if (g_prevHandler) {
    g_prevHandler(type, ctx, msg);
  }
}


int main(int argc, char *argv[]) {
  initLogLevelFromEnv();
  g_prevHandler = qInstallMessageHandler(filteredMessageHandler);
  qSetMessagePattern(
      QStringLiteral("%{time yyyy-MM-dd hh:mm:ss.zzz} [%{type}] %{message}"));
  auto ensureEnv = [](const char *name, const QByteArray &value) {
    if (!qEnvironmentVariableIsSet(name) ||
        qEnvironmentVariable(name).isEmpty())
      qputenv(name, value);
  };
  // 整合方案：使用 linuxfb/epaper 平台直接输出，不再用 offscreen + 共享内存
  // 优先使用启动脚本设置的 QT_QPA_PLATFORM（如
  // linuxfb:fb=/dev/fb0:size=954x1696:depth=16） 如需回退到旧方案，设置环境变量
  // QT_QPA_PLATFORM=offscreen
  ensureEnv("QT_QPA_PLATFORM", QByteArrayLiteral("linuxfb"));
  ensureEnv("QTWEBENGINE_DISABLE_SANDBOX", QByteArrayLiteral("1"));
  // 强制设置 Chromium flags，避免启动脚本未传递导致缺失
  // 方案E优化：启用HTTP缓存、HTTP/2、Brotli压缩，增加渲染线程数
  QByteArray chromiumFlags = QByteArrayLiteral(
      "--disable-gpu --disable-gpu-compositing "
      "--disable-webgl --disable-accelerated-video-decode "
      "--disable-smooth-scrolling --num-raster-threads=2 "
      "--no-sandbox --touch-events=enabled "
      // 启用 HTTP 缓存，降低脚本截断概率
      "--disk-cache-size=52428800 "
      "--disable-background-networking --disable-sync --disable-translate "
      "--disable-component-update --disable-default-apps --disable-extensions "
      "--disable-client-side-phishing-detection "
      // 强制关闭 DoH/SVCB + AsyncDns + HappyEyeballsV3，避免解析失败走错误路径
      "--dns-over-https-mode=off "
      "--disable-features=UseDnsHttpsSvcb,AsyncDns,HappyEyeballsV3,MediaRouter "
      // 尽量禁用 IPv6（部分版本可能忽略该 flag）
      "--disable-ipv6 "
      // 禁用 QUIC，避免弱网络放大失败
      "--disable-quic "
      // 禁用 HTTP/2，规避协议异常导致脚本资源为空
      "--disable-http2 "
      "--enable-features=NetworkServiceInProcess");
  // 2D Canvas：默认关闭加速，设置 WEREAD_ENABLE_ACCEL_2D=1 可开启
  if (qEnvironmentVariableIsSet("WEREAD_ENABLE_ACCEL_2D") &&
      qEnvironmentVariableIntValue("WEREAD_ENABLE_ACCEL_2D") != 0) {
    chromiumFlags.append(" --enable-accelerated-2d-canvas");
    qInfo() << "[ENV] 2D canvas accel enabled (WEREAD_ENABLE_ACCEL_2D=1)";
  } else {
    chromiumFlags.append(" --disable-accelerated-2d-canvas");
    qInfo() << "[ENV] 2D canvas accel disabled (default; set "
               "WEREAD_ENABLE_ACCEL_2D=1 to enable)";
  }
  const QByteArray netlogPath = qgetenv("WEREAD_NETLOG");
  if (!netlogPath.isEmpty()) {
    chromiumFlags.append(" --log-net-log=");
    chromiumFlags.append(netlogPath);
    chromiumFlags.append(" --net-log-capture-mode=IncludeSensitive");
    qInfo() << "[ENV] netlog enabled" << netlogPath;
  }
  qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromiumFlags);
  qInfo() << "[ENV] QTWEBENGINE_CHROMIUM_FLAGS"
          << qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
  QCoreApplication::setAttribute(Qt::AA_SynthesizeTouchForUnhandledMouseEvents);
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("weread-lab");
  QCoreApplication::setApplicationName("weread-browser");
  // 整合方案：移除 ShmFrameWriter，不再写共享内存
  // 显示由 epaper 平台直接输出到 fb0，由 appload shim 拦截并刷新
  FbRefreshHelper fbRef; // 简单刷新帮助器：启动后做一次全屏刷新
  // Resolve start URL: 根据退出方式决定是否恢复会话
  const QByteArray envUrl = qgetenv("WEREAD_URL");
  QUrl startUrl;

  // 读取退出方式标记
  QString exitReason = WereadBrowser::loadExitReason();
  qInfo() << "[WEREAD] Last exit reason:" << exitReason;

  // 尝试加载保存的会话 URL（如果存在且有效）
  QUrl sessionUrl;
  bool shouldRestoreSession = (exitReason == QStringLiteral("swipe_up"));

  if (shouldRestoreSession) {
    // 上滑退出：尝试恢复会话
    WereadBrowser tempBrowser(QUrl("about:blank"), nullptr);
    sessionUrl = tempBrowser.loadSessionUrl();
    if (sessionUrl.isValid()) {
      qInfo() << "[WEREAD] Will restore session (swipe_up exit)";
    } else {
      qInfo() << "[WEREAD] No valid session to restore, will start fresh";
    }
  } else {
    // 关闭按钮退出或不明原因：不恢复会话，直接进入首页
    qInfo() << "[WEREAD] Will start fresh (exit reason:" << exitReason << ")";
  }

  if (qEnvironmentVariableIsSet("WEREAD_START_BLANK")) {
    startUrl = QUrl(QStringLiteral("about:blank"));
  } else if (!envUrl.isEmpty()) {
    startUrl = QUrl::fromUserInput(QString::fromLocal8Bit(envUrl));
  } else if (argc > 1) {
    startUrl = QUrl::fromUserInput(QString::fromLocal8Bit(argv[1]));
  } else if (shouldRestoreSession && sessionUrl.isValid()) {
    startUrl = sessionUrl; // 使用保存的会话 URL
    qInfo() << "[WEREAD] Restoring session URL:" << startUrl.toString();
  } else {
    startUrl =
        QUrl(QStringLiteral("https://weread.qq.com/")); // 默认进入微信读书首页
    qInfo() << "[WEREAD] Starting with default URL (homepage)";
  }
  qInfo() << "[WEREAD] Starting with URL:" << startUrl.toString();
  // 一体化方案：无需后端 viewer
  WereadBrowser window(startUrl, &fbRef);

  // 如果恢复了会话 URL，也恢复滚动位置
  if (shouldRestoreSession && sessionUrl.isValid()) {
    window.m_restoredScrollY = window.loadSessionScrollPosition();
    window.m_isRestoringSession = true;
    if (window.m_restoredScrollY > 0) {
      qInfo() << "[SESSION] Will restore scroll position:"
              << window.m_restoredScrollY;
    }
  }

  // 安装 KOReader 风格手势过滤器（状态机驱动，区分 tap/pan/swipe/hold）
  // 在应用级别安装，确保能够捕获所有触摸/鼠标事件（后安装的过滤器会先被调用）
  GestureFilter *appGestureFilter = new GestureFilter(&window, &app);
  app.installEventFilter(appGestureFilter);
  qInfo() << "[GESTURE] KOReader-style GestureFilter installed at application "
             "level";

  // 移除视图级别的事件过滤器，避免事件被重复处理导致状态混乱
  // 应用级别的事件过滤器已经能够捕获所有事件，包括菜单和手势事件

  window.show(); // epaper platform, show() triggers painting to fb0
  window.raise();
  window.activateWindow();
  QTimer::singleShot(800, [&window, &fbRef]() {
    const int w = window.width();
    const int h = window.height();
    qInfo() << "[EINK] Startup: initial full refresh";
    fbRef.refreshFull(w, h);
  });
  int result = app.exec();
  return result;
}
