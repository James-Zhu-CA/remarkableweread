#include "weread_browser.h"

void WereadBrowser::initProfileAndView(const QUrl &url) {
  setWindowTitle("WeRead - Paper Pro");
  setAttribute(Qt::WA_AcceptTouchEvents, true);
  // Portrait logical size matching device (width x height)
  resize(954, 1696);
  // Configure profile: allow switching between network URL and local file://
  m_profile = new QWebEngineProfile(QStringLiteral("weread-profile"), this);
  // 启用持久化存储和缓存，实现会话恢复
  QString dataDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dataDir);
  QString cacheDir = dataDir + "/cache";
  QDir().mkpath(cacheDir);
  QString storageDir = dataDir + "/storage";
  QDir().mkpath(storageDir);
  m_profile->setPersistentStoragePath(storageDir); // LocalStorage, IndexedDB 等
  m_profile->setCachePath(cacheDir);               // HTTP 缓存路径
  m_profile->setPersistentCookiesPolicy(
      QWebEngineProfile::AllowPersistentCookies);
  m_profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache); // 启用磁盘缓存
  // 设置缓存大小：100MB (0 = 自动管理)
  m_profile->setHttpCacheMaximumSize(100 * 1024 * 1024);
  qInfo() << "[CACHE] HTTP cache config: type=DiskHttpCache path=" << cacheDir
          << "maxSize=100MB";
  const QString cacheClearMarker = dataDir + "/cache_cleared.v1";
  const bool forceClearCache =
      qEnvironmentVariableIsSet("WEREAD_CLEAR_CACHE_ON_START") &&
      qEnvironmentVariableIntValue("WEREAD_CLEAR_CACHE_ON_START") != 0;
  if (forceClearCache || !QFile::exists(cacheClearMarker)) {
    QDir cache(cacheDir);
    if (cache.exists()) {
      cache.removeRecursively();
    }
    QDir().mkpath(cacheDir);
    m_profile->clearHttpCache();
    QFile marker(cacheClearMarker);
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      marker.write("cleared\n");
      marker.close();
    }
    qInfo() << "[CACHE] cleared disk cache"
            << "force" << forceClearCache << "marker" << cacheClearMarker;
  }
  // 安装请求拦截器，阻断字体 / 统计 / 媒体等资源
  ResourceInterceptor *interceptor = new ResourceInterceptor(this, this);
  m_profile->setUrlRequestInterceptor(interceptor);
  // 暂时移除所有注入脚本，排查 SyntaxError/空白根因（资源加载/缓存/上游）
  const QString uaDefault = m_profile->httpUserAgent();
  m_kindleUA = QStringLiteral(
      "Mozilla/5.0 (Linux; Kindle Paperwhite) AppleWebKit/537.36 (KHTML, like "
      "Gecko) Silk/3.2 Mobile Safari/537.36");
  m_ipadMiniUA = QStringLiteral(
      "Mozilla/5.0 (iPad; CPU OS 17_0 like Mac OS X) AppleWebKit/605.1.15 "
      "(KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1");
  const QString androidUA = QStringLiteral(
      "Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, "
      "like Gecko) Chrome/129.0.0.0 Mobile Safari/537.36");
  m_desktopChromeUA =
      QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                     "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
  const QByteArray uaModeEnv = qgetenv("WEREAD_UA_MODE");
  const QString uaMode = QString::fromLatin1(uaModeEnv);
  const QByteArray bookModeEnv = qgetenv("WEREAD_BOOK_MODE");
  const QString bookMode = QString::fromLatin1(bookModeEnv);
  // 默认 UA：非书籍页使用桌面 UA，书籍页使用 Kindle UA
  Q_UNUSED(uaMode);
  Q_UNUSED(bookMode);
  m_uaNonWeRead = m_desktopChromeUA;
  // 微信读书书籍页 UA：强制 Kindle，保证按钮/布局可控
  m_weReadBookMode = QStringLiteral("mobile");
  m_uaWeReadBook = m_kindleUA.isEmpty() ? m_uaNonWeRead : m_kindleUA;
  // 得到书籍页 UA：独立使用 Android UA（移动端）
  m_uaDedaoBook = androidUA;
  // 初始 UA 先按目标 URL 决定
  m_currentUA = QStringLiteral("unset");
  updateUserAgentForUrl(url);
  qInfo() << "[UA] default" << uaDefault;
  qInfo() << "[UA] mode"
          << (uaMode.isEmpty() ? QStringLiteral("default") : uaMode)
          << "non-weRead" << m_uaNonWeRead << "weRead-book" << m_uaWeReadBook
          << "dedao-book" << m_uaDedaoBook << "bookMode" << m_weReadBookMode;
  qInfo() << "[PROFILE] data" << m_profile->persistentStoragePath() << "cache"
          << m_profile->cachePath() << "cookiesPolicy"
          << m_profile->persistentCookiesPolicy() << "offTheRecord"
          << m_profile->isOffTheRecord();

  // 配置翻页 ping 策略（由环境变量控制）
  {
    bool ok = false;
    int interval = qEnvironmentVariableIntValue("WEREAD_PING_INTERVAL", &ok);
    if (ok && interval > 0) {
      m_pingInterval = interval;
    }
    // 通过 WEREAD_PING_DISABLE=1 禁用 ping
    m_pingDisabled = qEnvironmentVariableIsSet("WEREAD_PING_DISABLE");
    qInfo() << "[PING] interval" << m_pingInterval << "disabled"
            << m_pingDisabled;
  }

  m_view = new QWebEngineView(this);
  RoutedPage *routedPage = new RoutedPage(m_profile, m_view, m_view);
  m_view->setPage(routedPage);
  setCentralWidget(m_view);
  m_view->setFixedSize(954, 1696);
  m_view->setAttribute(Qt::WA_AcceptTouchEvents, true);
  m_view->setFocusPolicy(Qt::StrongFocus);
  m_view->setFocus(Qt::OtherFocusReason);

  // 安装触摸/鼠标日志过滤器，不改变事件流，仅记录
  // TouchLogger: 仅在设置 WEREAD_TOUCH_DEBUG=1 时启用（调试触摸问题用）
  // 默认禁用以减少高频日志输出和事件过滤器开销
  if (qEnvironmentVariableIsSet("WEREAD_TOUCH_DEBUG") &&
      qEnvironmentVariableIntValue("WEREAD_TOUCH_DEBUG") != 0) {
    m_view->installEventFilter(new TouchLogger(m_view));
    if (QWidget *proxy = m_view->focusProxy()) {
      proxy->setAttribute(Qt::WA_AcceptTouchEvents, true);
      proxy->setFocusPolicy(Qt::StrongFocus);
      proxy->setFocus(Qt::OtherFocusReason);
      proxy->installEventFilter(new TouchLogger(proxy));
      qInfo() << "[TOUCH_EVT] focusProxy touch logger enabled";
    } else {
      qWarning() << "[TOUCH_EVT] focusProxy not found";
    }
    bool quickWidgetFound = false;
    const QList<QWidget *> childWidgets = m_view->findChildren<QWidget *>();
    for (QWidget *child : childWidgets) {
      const char *className =
          child ? child->metaObject()->className() : nullptr;
      if (className &&
          QString::fromLatin1(className) == QStringLiteral("QQuickWidget")) {
        quickWidgetFound = true;
        child->setAttribute(Qt::WA_AcceptTouchEvents, true);
        child->setFocusPolicy(Qt::StrongFocus);
        child->installEventFilter(new TouchLogger(child));
        qInfo() << "[TOUCH_EVT] QQuickWidget touch logger enabled" << className
                << "name" << child->objectName();
      }
    }
    if (!quickWidgetFound) {
      qWarning() << "[TOUCH_EVT] QQuickWidget not found under view";
    }
    qInfo() << "[TOUCH_EVT] touch logger enabled (WEREAD_TOUCH_DEBUG=1)";
  } else {
    qInfo() << "[TOUCH_EVT] touch logger disabled (set WEREAD_TOUCH_DEBUG=1 to "
               "enable)";
  }
  installWeReadDefaultSettingsScript();
  installDedaoDefaultSettingsScript();
  installChapterObserverScript();

  // 创建智能刷新管理器
  if (m_fbRef) {
    m_smartRefreshWeRead = new SmartRefreshManager(
        m_fbRef, 954, 1696, QStringLiteral("weread"), this);
    m_smartRefreshDedao = new SmartRefreshManager(
        m_fbRef, 954, 1696, QStringLiteral("dedao"), this);
    m_smartRefreshDedao->setPostClickA2Enabled(false);
    // 连接 RoutedPage 的智能刷新信号
    connect(routedPage, &RoutedPage::smartRefreshEvents, this,
            [this](const QString &json) {
              noteDomEventFromJson(json);
              handleSmartRefreshEvents(json);
            });
    connect(
        routedPage, &RoutedPage::chapterInfosMapped, this, [this](int count) {
          if (!m_fbRef)
            return;
          const qint64 now = QDateTime::currentMSecsSinceEpoch();
          if (now - m_lastChapterInfosRefreshMs < 3000) {
            qInfo()
                << "[EINK] refresh after chapterInfos mapped skipped (throttle)"
                << count;
            return;
          }
          m_lastChapterInfosRefreshMs = now;
          QTimer::singleShot(120, this, [this, count]() {
            if (m_fbRef) {
              m_fbRef->refreshUI(0, 0, width(), height());
              qInfo() << "[EINK] refresh after chapterInfos mapped" << count;
            }
          });
        });
    connect(routedPage, &RoutedPage::smartRefreshBurstEnd, this,
            [this]() { handleSmartRefreshBurstEnd(); });
    connect(routedPage, &RoutedPage::jsUnexpectedEnd, this,
            [this](const QString &msg, const QString &src) {
              handleJsUnexpected(msg, src);
            });
    qInfo() << "[SMART_REFRESH] Connected to RoutedPage signals";
  }
  QWebEngineSettings *settings = m_view->settings();
  // 可通过 WEREAD_DISABLE_JS 临时关闭 JavaScript，验证解析错误
  const bool disableJs = qEnvironmentVariableIsSet("WEREAD_DISABLE_JS");
  settings->setAttribute(QWebEngineSettings::JavascriptEnabled, !disableJs);
  settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
  settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);
  // 允许 window.open；在 createWindow / acceptNavigation 中做定制拦截
  settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, true);
  settings->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, false);
  settings->setAttribute(QWebEngineSettings::AutoLoadIconsForPage, false);
  // Use moderate zoom for readability; touch mapping accounts for zoom in
  // hittest
  m_view->setZoomFactor(1.5);
}
