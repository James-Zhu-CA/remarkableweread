#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <QWebEngineView>
#include <QWebEngineNavigationRequest>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QTimer>
#include <QWebEngineHistory>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QList>
#include <QStandardPaths>
#include <QUrl>
#include <QString>
#include <QDebug>
#include <QUdpSocket>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QHostAddress>
#include <QPushButton>
#include <QProcess>
#include <QFrame>
#include <QHBoxLayout>
#include <QDateTime>
#include <QRegularExpression>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include <QtGlobal>
#include "shm_writer.h"
// Suppress high-volume flip logs to save resources.
static QtMessageHandler g_prevHandler = nullptr;
static void filteredMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
    if (msg.contains(QStringLiteral("[PAGER]")) || msg.contains(QStringLiteral("[TOUCH]"))) {
        return;
    }
    if (g_prevHandler) {
        g_prevHandler(type, ctx, msg);
    }
}

// --- 【性能优化】网络请求拦截器 ---
// 条件编译开关：取消注释下行以启用详细资源日志（会影响性能）
// #define WEREAD_DEBUG_RESOURCES

class ResourceInterceptor : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT
public:
    explicit ResourceInterceptor(QObject *parent = nullptr) : QWebEngineUrlRequestInterceptor(parent) {}

    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        const QString url = info.requestUrl().toString();

#ifdef WEREAD_DEBUG_RESOURCES
        const qint64 reqStartTs = QDateTime::currentMSecsSinceEpoch();
        // 获取资源类型的字符串表示（用于诊断）
        const QString resourceTypeStr = [&]() {
            switch(info.resourceType()) {
                case QWebEngineUrlRequestInfo::ResourceTypeMainFrame: return "MainFrame";
                case QWebEngineUrlRequestInfo::ResourceTypeSubFrame: return "SubFrame";
                case QWebEngineUrlRequestInfo::ResourceTypeStylesheet: return "Stylesheet";
                case QWebEngineUrlRequestInfo::ResourceTypeScript: return "Script";
                case QWebEngineUrlRequestInfo::ResourceTypeImage: return "Image";
                case QWebEngineUrlRequestInfo::ResourceTypeFontResource: return "Font";
                case QWebEngineUrlRequestInfo::ResourceTypeMedia: return "Media";
                case QWebEngineUrlRequestInfo::ResourceTypeXhr: return "XHR";
                default: return "Other";
            }
        }();
        qInfo() << "[RESOURCE_START]" << resourceTypeStr << url.mid(0, 100) << "ts" << reqStartTs;
#endif

        auto resourceTypeStr = [&]() {
            switch(info.resourceType()) {
                case QWebEngineUrlRequestInfo::ResourceTypeMainFrame: return "MainFrame";
                case QWebEngineUrlRequestInfo::ResourceTypeSubFrame: return "SubFrame";
                case QWebEngineUrlRequestInfo::ResourceTypeStylesheet: return "Stylesheet";
                case QWebEngineUrlRequestInfo::ResourceTypeScript: return "Script";
                case QWebEngineUrlRequestInfo::ResourceTypeImage: return "Image";
                case QWebEngineUrlRequestInfo::ResourceTypeFontResource: return "Font";
                case QWebEngineUrlRequestInfo::ResourceTypeMedia: return "Media";
                case QWebEngineUrlRequestInfo::ResourceTypeXhr: return "XHR";
                default: return "Other";
            }
        };

        // Block payment scripts
        if (url.contains(QStringLiteral("midas.gtimg.cn")) ||
            url.contains(QStringLiteral("minipay")) ||
            url.contains(QStringLiteral("cashier.js"))) {
            qWarning() << "[BLOCK]" << resourceTypeStr() << url;
            info.block(true);
            return;
        }

        // Block winktemplaterendersvr fonts/media (KaTeX related)
        if (url.contains(QStringLiteral("/winktemplaterendersvr/")) &&
            (info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeFontResource ||
             info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeMedia)) {
            qWarning() << "[BLOCK]" << resourceTypeStr() << url;
            info.block(true);
            return;
        }

        // Block SourceHanSerif heavy fonts (~10MB each)
        if (url.contains(QStringLiteral("SourceHanSerif"))) {
            qWarning() << "[BLOCK]" << resourceTypeStr() << url;
            info.block(true);
            return;
        }

        // 进度接口早期记录（包括 ServiceWorker 发出的请求）
        if (url.contains(QStringLiteral("/web/book/read")) || url.contains(QStringLiteral("getProgress"))) {
            qInfo() << "[PROGRESS_NET]" << resourceTypeStr() << url;
        }

#ifdef WEREAD_DEBUG_RESOURCES
        qInfo() << "[RESOURCE_ALLOWED]" << resourceTypeStr << "ts" << reqStartTs;
#endif
    }
};

void checkMenuPanelState(QWebEnginePage *page) {
    if (!page) return;
    // 简化字符串，避免模板字符串解析歧义
    const QString js = QStringLiteral(
        "(function(){"
        "  const panel=document.querySelector('.set-tool-box');"
        "  if(!panel) return '[CLICK_DEBUG] Panel .set-tool-box NOT found';"
        "  const s=getComputedStyle(panel);"
        "  const r=panel.getBoundingClientRect();"
        "  return '[CLICK_DEBUG] Panel Found. display:' + s.display +"
        "         ' vis:' + s.visibility +"
        "         ' op:' + s.opacity +"
        "         ' z:' + s.zIndex +"
        "         ' Rect: x=' + r.left + ' y=' + r.top + ' w=' + r.width + ' h=' + r.height;"
        "})()"
    );
    page->runJavaScript(js, [](const QVariant &res){
        qInfo().noquote() << res.toString();
    });
}

class RoutedPage : public QWebEnginePage {
    Q_OBJECT
public:
    RoutedPage(QWebEngineProfile* profile, QWebEngineView* view, QObject* parent = nullptr)
        : QWebEnginePage(profile, parent), m_view(view) {}
protected:
    QWebEnginePage* createWindow(WebWindowType type) override {
        Q_UNUSED(type);
        // Block popups on dedao book pages to prevent navigation away
        const QString currentUrl = m_view ? m_view->url().toString() : QString();
        const bool isDedaoBook = currentUrl.contains(QStringLiteral("dedao.cn")) &&
                                 currentUrl.contains(QStringLiteral("/ebook/reader"));

        if (isDedaoBook) {
            qInfo() << "[WINDOW] blocked popup on dedao book page" << currentUrl;
            return nullptr; // Block the popup
        }

        // For other pages, redirect to main view
        auto *tmp = new QWebEnginePage(profile(), this);
        connect(tmp, &QWebEnginePage::urlChanged, this, [this, tmp](const QUrl &url){
            if (m_view) {
                qInfo() << "[WINDOW] redirect new window to current view" << url;
                m_view->load(url);
            }
            tmp->deleteLater();
        });
        return tmp;
    }
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override {
        if (!isMainFrame) return true;
        const qint64 ts = QDateTime::currentMSecsSinceEpoch();
        const qint64 rel = m_lastReloadTs ? (ts - m_lastReloadTs) : -1;
        qInfo() << "[NAV] request" << url << "type" << type << "mainFrame" << isMainFrame
                << "ts" << ts << "sinceReload" << rel;
        const QString currentUrl = m_view ? m_view->url().toString() : QString();
        const bool inDedaoReader = currentUrl.contains(QStringLiteral("dedao.cn")) &&
                                   currentUrl.contains(QStringLiteral("/ebook/reader"));
        if (inDedaoReader) {
            const QString target = url.toString();
            const bool sameReader = target.contains(QStringLiteral("dedao.cn")) &&
                                    target.contains(QStringLiteral("/ebook/reader"));
            const bool isDetail = target.contains(QStringLiteral("/ebook/detail"));
            if (!sameReader) {
                qInfo() << "[NAV] block non-reader nav from dedao" << url << "ts" << ts << "sinceReload" << rel;
                return false;
            }
            if (isDetail) {
                qInfo() << "[NAV] block detail nav inside reader" << url << "ts" << ts << "sinceReload" << rel;
                return false;
            }
        }
        return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
    }
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                  const QString &message,
                                  int lineNumber,
                                  const QString &sourceID) override {
        Q_UNUSED(level);
        // 定向屏蔽 weread 书页的 SyntaxError 噪声，防止日志刷屏
        const bool isWeReadSrc = sourceID.contains(QStringLiteral("weread.qq.com"), Qt::CaseInsensitive);
        if (isWeReadSrc && message.contains(QStringLiteral("SyntaxError"), Qt::CaseInsensitive)) {
            return;
        }
        qWarning().noquote() << "[JS]" << sourceID << ":" << lineNumber << message;
    }
private:
    QWebEngineView* m_view = nullptr;
    qint64 m_bookEnterTs = 0;
    qint64 m_lastReloadTs = 0; // pointer to browser not available here, so keep local timestamp unused
};

class WereadBrowser : public QMainWindow {
    Q_OBJECT
public:
    WereadBrowser(const QUrl& url, ShmFrameWriter* writer) : QMainWindow(), m_writer(writer) {
        setWindowTitle("WeRead - Paper Pro");
        // Portrait logical size matching device (width x height)
        resize(954, 1696);
        // Configure profile: allow switching between network URL and local file://
        m_profile = new QWebEngineProfile(QStringLiteral("weread-profile"), this);
        // 为排查缓存损坏，改用内存缓存/无持久路径
        m_profile->setPersistentStoragePath(QString());
        m_profile->setCachePath(QString());
        m_profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
        m_profile->setHttpCacheType(QWebEngineProfile::NoCache);
        // 安装请求拦截器，阻断字体 / 统计 / 媒体等资源
        ResourceInterceptor *interceptor = new ResourceInterceptor(this);
        m_profile->setUrlRequestInterceptor(interceptor);
        // 暂时移除所有注入脚本，排查 SyntaxError/空白根因（资源加载/缓存/上游）
        const QString uaDefault = m_profile->httpUserAgent();
        m_kindleUA = QStringLiteral("Mozilla/5.0 (Linux; Kindle Paperwhite) AppleWebKit/537.36 (KHTML, like Gecko) Silk/3.2 Mobile Safari/537.36");
        m_ipadMiniUA = QStringLiteral("Mozilla/5.0 (iPad; CPU OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1");
        const QString androidUA = QStringLiteral("Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/129.0.0.0 Mobile Safari/537.36");
        const QString desktopChromeUA = QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        const QByteArray uaModeEnv = qgetenv("WEREAD_UA_MODE");
        const QString uaMode = QString::fromLatin1(uaModeEnv);
        const QByteArray bookModeEnv = qgetenv("WEREAD_BOOK_MODE");
        const QString bookMode = QString::fromLatin1(bookModeEnv);
        // 默认 UA 切换为桌面 Chrome，避免移动端新特性/ES 模块路径
        if (uaMode.compare(QStringLiteral("kindle"), Qt::CaseInsensitive) == 0) {
            m_uaNonWeRead = m_kindleUA;
        } else {
            m_uaNonWeRead = desktopChromeUA;
        }
        // 微信读书书籍页 UA：也使用桌面 Chrome，保持一致
        if (bookMode.compare(QStringLiteral("web"), Qt::CaseInsensitive) == 0) {
            m_weReadBookMode = QStringLiteral("web");
            m_uaWeReadBook = desktopChromeUA;
        } else {
            m_weReadBookMode = QStringLiteral("mobile");
            m_uaWeReadBook = desktopChromeUA;
        }
        // 初始 UA 先按目标 URL 决定
        m_currentUA = QStringLiteral("unset");
        updateUserAgentForUrl(url);
        qInfo() << "[UA] default" << uaDefault;
        qInfo() << "[UA] mode" << (uaMode.isEmpty() ? QStringLiteral("default") : uaMode)
                << "non-weRead" << m_uaNonWeRead << "weRead-book" << m_uaWeReadBook
                << "bookMode" << m_weReadBookMode;
        qInfo() << "[PROFILE] data" << m_profile->persistentStoragePath()
                << "cache" << m_profile->cachePath()
                << "cookiesPolicy" << m_profile->persistentCookiesPolicy()
                << "offTheRecord" << m_profile->isOffTheRecord();

        m_view = new QWebEngineView(this);
        m_view->setPage(new RoutedPage(m_profile, m_view, m_view));
        setCentralWidget(m_view);
        m_view->setFixedSize(954, 1696);
        m_view->setFocusPolicy(Qt::StrongFocus);
        m_view->setFocus(Qt::OtherFocusReason);
        QWebEngineSettings* settings = m_view->settings();
        // 可通过 WEREAD_DISABLE_JS 临时关闭 JavaScript，验证解析错误
        const bool disableJs = qEnvironmentVariableIsSet("WEREAD_DISABLE_JS");
        settings->setAttribute(QWebEngineSettings::JavascriptEnabled, !disableJs);
        settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
        settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);
        // 允许 window.open；在 createWindow / acceptNavigation 中做定制拦截
        settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, true);
        settings->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, false);
        settings->setAttribute(QWebEngineSettings::AutoLoadIconsForPage, false);
        // Use moderate zoom for readability; touch mapping accounts for zoom in hittest
        m_view->setZoomFactor(1.6);
        m_captureTimer = new QTimer(this);
        m_captureTimer->setSingleShot(true);
        connect(m_view, &QWebEngineView::loadStarted, this, [this](){
            const qint64 ts = QDateTime::currentMSecsSinceEpoch();
            qInfo() << "[TIMING] loadStarted url" << currentUrl << "ts" << ts;
            m_bookEarlyReloads = 0;
            m_iframeFixApplied = false;
            m_diagRan = false;
            // 如果设置了 WEREAD_BLANK_HOOK，则直接 setHtml 覆盖空白，彻底绕过远端文档解析
            const QByteArray blankHook = qgetenv("WEREAD_BLANK_HOOK");
            if (!blankHook.isEmpty()) {
                qWarning() << "[HOOK] Applying setHtml blank (WEREAD_BLANK_HOOK set)";
                if (m_view && m_view->page()) {
                    m_view->setHtml(QStringLiteral("<!DOCTYPE html><html><body>blank hook</body></html>"),
                                    QUrl(QStringLiteral("https://weread.qq.com/")));
                }
            }
        });
        connect(m_view, &QWebEngineView::loadProgress, this, [this](int p){
            const qint64 ts = QDateTime::currentMSecsSinceEpoch();
            qInfo() << "[TIMING] loadProgress" << p << "url" << currentUrl << "ts" << ts;
        });

        // Phase 2: JavaScript console capture (via performance monitoring JS logs captured as console.log)
        // Note: javaScriptConsoleMessage is protected in Qt6, so we capture logs via injected JS console.log

        connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
            if (ok) {
                if (qEnvironmentVariableIsSet("WEREAD_START_BLANK")) {
                    qInfo() << "[INFO] startBlank mode: skip integrity/ready";
                    // 仍然抓一次帧，避免空白显示需要手动点击
                    scheduleBookCaptures();
                    return;
                }
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                qInfo() << "[TIMING] loadFinished url" << currentUrl << "ts" << now;
                m_firstFrameDone = false;
                m_reloadAttempts = 0;
                logUrlState(QStringLiteral("loadFinished"));
                sendBookState();
                if (isDedaoBook()) {
                    ensureDedaoDefaults();
                }

                // Phase 3: Inject performance monitoring JavaScript
                const QString perfMonitorJs = QStringLiteral(R"(
(function() {
    const logPerf = (event, data) => {
        console.log('[PERF_JS]', event, JSON.stringify(data), Date.now());
    };

    // Track DOM ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', () => {
            logPerf('DOMContentLoaded', {readyState: document.readyState});
        });
    } else {
        logPerf('DOMContentLoaded_Already', {readyState: document.readyState});
    }

    // Track when WeRead content becomes visible
    const checkReady = setInterval(() => {
        const readerContent = document.querySelector('.renderTargetContent, #app, .app_content, .content');
        if (readerContent && readerContent.offsetHeight > 0) {
            logPerf('WeRead_Content_Found', {selector: readerContent.className, height: readerContent.offsetHeight});
            clearInterval(checkReady);
        }
    }, 100);

    // Track canvas operations
    const CanvasRenderingContext2D_original = CanvasRenderingContext2D.prototype.drawImage;
    if (CanvasRenderingContext2D_original) {
        CanvasRenderingContext2D.prototype.drawImage = function() {
            logPerf('Canvas_DrawImage', {args: arguments.length});
            return CanvasRenderingContext2D_original.apply(this, arguments);
        };
    }

    setTimeout(() => clearInterval(checkReady), 60000);
})();
)");
                m_view->page()->runJavaScript(perfMonitorJs);

                // 总是抓一次帧，避免 ready 失败导致必须点击才显示
                scheduleBookCaptures();
                // 内容完整性检查：非书籍页走旧逻辑，书籍页单独做核心探针后再决定是否重载
                if (!isWeReadBook() && !isDedaoBook()) {
                    m_view->page()->runJavaScript(QStringLiteral(
                        "(() => {"
                        "  try {"
                        "    const bodyLen = (document.body && document.body.innerText) ? document.body.innerText.length : 0;"
                        "    const hasNuxt = !!(window.__NUXT__);"
                        "    return {bodyLen, hasNuxt, href: location.href||''};"
                        "  } catch(e) { return {error:String(e)}; }"
                        "})()"
                    ), [this, now](const QVariant &res){
                        const QVariantMap m = res.toMap();
                        const int bodyLen = m.value(QStringLiteral("bodyLen")).toInt();
                        const bool hasNuxt = m.value(QStringLiteral("hasNuxt")).toBool();
                        if (m_integrityReloads < 2 && (bodyLen < 200 && !hasNuxt)) {
                            m_integrityReloads++;
                            qWarning() << "[INTEGRITY] fail bodyLen" << bodyLen << "hasNuxt" << hasNuxt
                                       << "reload bypass cache attempt" << m_integrityReloads;
                            m_view->page()->triggerAction(QWebEnginePage::ReloadAndBypassCache);
                            m_lastReloadTs = now;
                            return;
                        }
                    });
                } else if (isWeReadBook()) {
                    // 书籍页：loadFinished 后早期自检正文/iframe，若全部为空且尚未重载过，尝试一次绕缓存重载
                    m_view->page()->runJavaScript(QStringLiteral(
                        "(() => {"
                        "  try {"
                        "    const bodyLen = (document.body && document.body.innerText) ? document.body.innerText.length : 0;"
                        "    const cont = document.querySelector('.renderTargetContent');"
                        "    const contLen = cont ? ((cont.innerText||'').length) : 0;"
                        "    let iframeText = 0;"
                        "    const frames = Array.from(document.querySelectorAll('iframe'));"
                        "    frames.forEach(f=>{"
                        "      try{ const d=f.contentDocument||f.contentWindow?.document; if(d&&d.body&&d.body.innerText){ iframeText += d.body.innerText.length; } }catch(_e){}"
                        "    });"
                        "    return {bodyLen, contLen, iframeText, frames: frames.length};"
                        "  } catch(e){ return {error:String(e)}; }"
                        "})()"
                    ), [this, now](const QVariant &res){
                        const auto m = res.toMap();
                        const int bodyLen = m.value(QStringLiteral("bodyLen")).toInt();
                        const int contLen = m.value(QStringLiteral("contLen")).toInt();
                        const int iframeText = m.value(QStringLiteral("iframeText")).toInt();
                        if (m_bookEarlyReloads < 1 && bodyLen == 0 && contLen == 0 && iframeText == 0) {
                            m_bookEarlyReloads++;
                            qWarning() << "[EARLY_RELOAD] book page empty, bypass cache attempt" << m_bookEarlyReloads
                                       << "frames" << m.value(QStringLiteral("frames")).toInt();
                            m_view->page()->triggerAction(QWebEnginePage::ReloadAndBypassCache);
                            m_lastReloadTs = now;
                            return;
                        }
                    });
                }
                const bool onWeReadHost = currentUrl.host().contains(QStringLiteral("weread.qq.com"), Qt::CaseInsensitive);

                // 阅读进度调试逻辑已移除以降低性能与耗电开销
                // 书籍页：即便未 ready，也先注入白底样式，确保白度判断可用
                if (isWeReadBook()) {
                    applyBookEnhancements();

                    // 优化2: 在页面稳定后延迟加载被拦截的脚本 (WASM/支付/Worker)
                    // 延迟3秒后加载，使 loadFinished 不再等待这些脚本
                    QTimer::singleShot(3000, [this]() {
                        qInfo() << "[OPTIMIZATION] 延迟加载被拦截的脚本 (WASM/支付/Worker)";
                        m_view->page()->runJavaScript(QStringLiteral(
                            "(function() {"
                            "  // 创建脚本加载函数"
                            "  const loadScript = (src) => {"
                            "    return new Promise((resolve) => {"
                            "      const script = document.createElement('script');"
                            "      script.src = src;"
                            "      script.onload = () => { qInfo('[DEFER-LOADED]', src); resolve(); };"
                            "      script.onerror = () => { console.warn('[DEFER-FAILED]', src); resolve(); };"
                            "      document.body.appendChild(script);"
                            "    });"
                            "  };"
                            "  "
                            "  // 需要加载的脚本列表 (被拦截的脚本)"
                            "  const deferredScripts = ["
                            "    'https://midas.gtimg.cn/midas/minipay_v2/jsapi/cashier.js',"  // 支付
                            "    'https://cdn.weread.qq.com/web/wpa-1.0.5.js',"               // WASM
                            "  ];"
                            "  "
                            "  // 如果这些脚本尚未加载，则加载它们"
                            "  deferredScripts.forEach((src) => {"
                            "    const existingScript = Array.from(document.scripts).some(s => s.src === src);"
                            "    if (!existingScript) {"
                            "      loadScript(src).catch(e => console.error('[DEFER-ERROR]', src, e));"
                            "    }"
                            "  });"
                            "})();"
                        ), [](const QVariant &res) {
                            // JavaScript 执行完成
                        });
                    });
                }
                startReadyWatch();
            }
            else { qWarning() << "[WEREAD] Page load failed"; }
        });
        // 导航拦截：在真正发起请求前阻止误跳转到得到详情页
        connect(m_view->page(), &QWebEnginePage::navigationRequested, this,
                [this](QWebEngineNavigationRequest &request) {
            const QString target = request.url().toString();
            const bool targetDedaoDetail = target.contains(QStringLiteral("dedao.cn")) &&
                                           target.contains(QStringLiteral("/ebook/detail"));
            // 仅当当前就在 dedao 阅读页时才拦截详情跳转，其他页面允许正常打开详情
            if (isDedaoBook() && targetDedaoDetail && !m_allowDedaoDetailOnce) {
                qWarning() << "[NAV_GUARD] reject navigationRequested to dedao detail" << target;
                request.reject();
                return;
            }
            if (targetDedaoDetail && m_allowDedaoDetailOnce) {
                qInfo() << "[NAV_GUARD] allow dedao detail navigation (once)" << target;
                m_allowDedaoDetailOnce = false;
            }
        });

        connect(m_view, &QWebEngineView::urlChanged, this, [this](const QUrl &url){
            const QString oldUrl = currentUrl.toString();
            const QString newUrlStr = url.toString();
            currentUrl = url;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            qInfo() << "[TIMING] urlChanged" << url << "ts" << now << "from" << oldUrl;
            // 记录最新的得到阅读页，便于回退
            if (newUrlStr.contains(QStringLiteral("dedao.cn")) &&
                newUrlStr.contains(QStringLiteral("/ebook/reader"))) {
                m_lastDedaoReaderUrl = newUrlStr;
            }
            // 在非阅读页允许进入 dedao 详情；仅在阅读页内时拦截
            const bool fromDedaoReader = oldUrl.contains(QStringLiteral("dedao.cn")) &&
                                         oldUrl.contains(QStringLiteral("/ebook/reader"));
            const bool toDedaoDetail = newUrlStr.contains(QStringLiteral("dedao.cn")) &&
                                       newUrlStr.contains(QStringLiteral("/ebook/detail"));
            if (fromDedaoReader && toDedaoDetail && !m_allowDedaoDetailOnce) {
                const QString fallback = !m_lastDedaoReaderUrl.isEmpty() ? m_lastDedaoReaderUrl : oldUrl;
                qWarning() << "[NAV_GUARD] block dedao detail urlChanged" << newUrlStr << "fallback" << fallback;
                if (m_view) {
                    m_view->stop();
                    m_view->setUrl(QUrl(fallback));
                }
                return;
            }
            if (toDedaoDetail && m_allowDedaoDetailOnce) {
                qInfo() << "[NAV_GUARD] allow dedao detail urlChanged once" << newUrlStr;
                m_allowDedaoDetailOnce = false;
            }
            logUrlState(QStringLiteral("urlChanged"));
            if (isWeReadBook() || isDedaoBook()) {
                m_bookEnterTs = now;
                qInfo() << "[TIMING] bookEnter ts" << now << "url" << url;
                // 进入书籍时立即启动抓帧循环，避免需手动触发
                scheduleBookCaptures();
            }
            updateUserAgentForUrl(url);
            sendBookState();
        });
        // 允许通过环境变量加载本地保存的 wr.html 以验证解析差异
        const QByteArray localHtmlEnv = qgetenv("WEREAD_LOCAL_HTML");
        const bool startBlank = qEnvironmentVariableIsSet("WEREAD_START_BLANK");
        if (startBlank) {
            qInfo() << "[WEREAD] Starting with about:blank (WEREAD_START_BLANK set)";
            m_view->setHtml(QStringLiteral("<!DOCTYPE html><html><body>blank start</body></html>"),
                            QUrl(QStringLiteral("about:blank")));
        } else if (!localHtmlEnv.isEmpty()) {
            const QUrl localUrl = QUrl::fromLocalFile(QString::fromLocal8Bit(localHtmlEnv));
            qInfo() << "[WEREAD] Loading local HTML for diagnostic:" << localUrl;
            m_view->load(localUrl);
        } else {
            m_view->load(url);
        }

        // Add exit button in top-left corner
        m_exitButton = new QPushButton("✕ 退出", this);
        m_exitButton->setGeometry(10, 10, 120, 50);
        m_exitButton->setStyleSheet(
            "QPushButton {"
            "  background-color: #f0f0f0;"
            "  border: 2px solid #333;"
            "  border-radius: 8px;"
            "  font-size: 20px;"
            "  font-weight: bold;"
            "  padding: 5px;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #d0d0d0;"
            "}"
        );
        m_exitButton->raise(); // Ensure button is on top
        connect(m_exitButton, &QPushButton::clicked, this, &WereadBrowser::onExitClicked);

        // Quick menu overlay (hidden by default)
        // menu overlay placed on top of the webview so it is captured in grab()
        m_menu = new QFrame(m_view);
        m_menu->setStyleSheet("QFrame { background: #ffffff; border: 0px solid transparent; border-radius: 12px; }"
                              "QPushButton { color: #000000; background: #e6e6e6; font-size: 22px; font-weight: 700; padding: 14px 18px; border-radius: 10px; }"
                              "QPushButton:pressed { background: #cfcfcf; }");
        auto *layout = new QHBoxLayout(m_menu);
        layout->setContentsMargins(16, 12, 16, 12);
        layout->setSpacing(12);
        auto addBtn = [&](const QString &text, const std::function<void()> &fn){
            auto *b = new QPushButton(text, m_menu);
            layout->addWidget(b);
            connect(b, &QPushButton::clicked, this, fn);
            return b;
        };
        addBtn(QStringLiteral("字体切换"), [this](){ toggleTheme(); }); // 暂时改为主题切换，便于在页面侧写入 cookies/LS
        addBtn(QStringLiteral("微信读书/得到"), [this](){ toggleService(); });
        auto *fontPlus = addBtn(QStringLiteral("字体 +"), [this](){ adjustFont(true); });
        if (fontPlus) {
            fontPlus->setStyleSheet("QPushButton { font-size:26px; font-weight:600; padding: 14px 18px; }"
                                    "QPushButton { letter-spacing: 2px; }");
        }
        auto *fontMinus = addBtn(QStringLiteral("字体 -"), [this](){ adjustFont(false); });
        if (fontMinus) {
            fontMinus->setStyleSheet("QPushButton { font-size:26px; font-weight:600; padding: 14px 18px; }"
                                     "QPushButton { letter-spacing: 2px; }");
        }
        m_uaButton = addBtn(QStringLiteral("UA"), [this](){ cycleUserAgentMode(); });
        // 根据当前 UA 推断模式，初始化按钮文案
        m_uaMode = detectUaMode(m_profile->httpUserAgent());
        updateUaButtonText(m_uaMode);
        m_menu->setVisible(false);
        m_menu->setFixedHeight(90);
        // initial size/position; will be updated in showMenu()
        m_menu->setGeometry(20, 20, 600, 90);
        // Allow menu buttons to receive clicks
        m_menuTimer.setInterval(5000);
        m_menuTimer.setSingleShot(true);
        connect(&m_menuTimer, &QTimer::timeout, this, [this](){ m_menu->hide(); });
        m_readyTimer.setInterval(250);
        m_readyTimer.setSingleShot(true);
        connect(&m_readyTimer, &QTimer::timeout, this, &WereadBrowser::pollReady);
        // Disable periodic heartbeat captures to avoid重复抓帧
        m_heartbeatTimer.setInterval(0);

        // listen for state requests on 45457 and respond with current state
        const QHostAddress bindAddr(QStringLiteral("127.0.0.1"));
        const bool ok = m_stateResponder.bind(bindAddr, 45457,
                                              QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
        qInfo() << "[STATE] responder bind" << ok << "addr" << bindAddr.toString() << "port" << 45457;
        connect(&m_stateResponder, &QUdpSocket::readyRead, this, &WereadBrowser::handleStateRequest);

        // 暂停所有注入脚本（探针/错误钩子），验证是否为注入引起的解析问题
    }
private slots:
    void captureFrame() {
        if (!m_writer) return;
        // 确保页面已经完全渲染：在offscreen平台，grab()可能抓取到未完成渲染的内容
        // 添加一个短暂的延迟，确保渲染管道已完成当前帧
        const QPixmap pix = m_view->grab();
        if (pix.isNull()) {
            qWarning() << "[SHM] grab returned null pixmap";
            return;
        }
        const QImage img = pix.toImage();
        if (img.isNull()) {
            qWarning() << "[SHM] grab produced null image";
            return;
        }
        
        // 验证图像尺寸是否正确
        if (img.width() != 954 || img.height() != 1696) {
            qWarning() << "[SHM] Unexpected image size:" << img.width() << "x" << img.height() << "expected 954x1696";
        }
        
        // Compute rough white ratio to detect empty/blank frames
        // 改进：同时统计不同亮度级别的像素，便于诊断
        double whiteRatio = 0.0;
        qint64 veryLight = 0;  // lum > 700 (near white)
        qint64 light = 0;      // 600 < lum <= 700
        qint64 medium = 0;     // 300 < lum <= 600
        qint64 dark = 0;       // lum <= 300
        {
            const int step = 8;
            qint64 white = 0;
            qint64 total = 0;
            for (int y = 0; y < img.height(); y += step) {
                const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
                for (int x = 0; x < img.width(); x += step) {
                    const QRgb px = line[x];
                    int r = qRed(px);
                    int g = qGreen(px);
                    int b = qBlue(px);
                    int lum = r + g + b;
                    if (lum > 700) { 
                        white++; 
                        veryLight++;
                    } else if (lum > 600) {
                        light++;
                    } else if (lum > 300) {
                        medium++;
                    } else {
                        dark++;
                    }
                    total++;
                }
            }
            if (total) whiteRatio = (white * 100.0) / total;
        }
        m_writer->publish(img);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_captureLoopActive) {
            m_captureFrameCount++;
            // Phase 4: Enhanced render frame logging with delta timing
            const qint64 captureDelta = m_lastCaptureTs ? (now - m_lastCaptureTs) : 0;
            m_lastCaptureTs = now;
            if (captureDelta > 0) {
                m_captureDeltaSum += captureDelta;
                m_captureDeltaCount++;
            }
            const qint64 frameSizeKB = img.sizeInBytes() / 1024;
            if (m_firstWhite < 0) m_firstWhite = whiteRatio;
            m_minWhite = std::min(m_minWhite, whiteRatio);
            m_maxWhite = std::max(m_maxWhite, whiteRatio);
            
            // 添加详细的像素分布诊断（仅在需要时输出，避免日志过多）
            static int diagnosticCounter = 0;
            bool shouldDiagnose = (whiteRatio > 90.0 && m_captureFrameCount % 10 == 0) || 
                                  (m_captureFrameCount <= 5) ||
                                  (whiteRatio < 50.0 && m_captureFrameCount % 20 == 0);
            
            if (shouldDiagnose) {
                qint64 total = veryLight + light + medium + dark;
                qInfo() << "[RENDER_FRAME]" << "white" << whiteRatio << "ts" << now
                        << "delta" << captureDelta << "frameSize" << frameSizeKB << "KB"
                        << "elapsed" << (now - m_captureLoopStart)
                        << "px_dist:" << QString("vLight:%1 light:%2 med:%3 dark:%4")
                           .arg(veryLight).arg(light).arg(medium).arg(dark)
                        << "size:" << img.width() << "x" << img.height();
            } else {
                qInfo() << "[RENDER_FRAME]" << "white" << whiteRatio << "ts" << now
                        << "delta" << captureDelta << "frameSize" << frameSizeKB << "KB"
                        << "elapsed" << (now - m_captureLoopStart);
            }
        }

        // 捕帧循环控制：每80ms抓帧，直到白度 <94 且连续3帧稳定，或超 60s
        // 优化：降低最小帧数与稳定帧数以缩短耗时（目标 ~1s）
        if (m_captureLoopActive) {
            if (m_lastWhite < 0) {
                m_lastWhite = whiteRatio;
                m_stableCount = 1;
            } else {
                if (std::abs(whiteRatio - m_lastWhite) <= 0.1) {
                    m_stableCount++;
                } else {
                    m_stableCount = 1;
                }
                m_lastWhite = whiteRatio;
            }
            const qint64 loopElapsed = now - m_captureLoopStart;
            const bool minFramesReached = m_captureFrameCount >= 10;
            bool stop = false;
            QString reason;
            if (whiteRatio < 93.0 && m_stableCount >= 3 && minFramesReached) { stop = true; reason = QStringLiteral("stable"); }
            if (loopElapsed >= 60000) { stop = true; reason = QStringLiteral("timeout60s"); }
            if (stop) {
                const double avgDelta = (m_captureDeltaCount > 0) ? (m_captureDeltaSum * 1.0 / m_captureDeltaCount) : 0.0;
                qInfo() << "[CAP] loop stop" << reason
                        << "elapsed" << loopElapsed
                        << "white" << whiteRatio
                        << "stable" << m_stableCount
                        << "frames" << m_captureFrameCount
                        << "firstWhite" << m_firstWhite
                        << "minWhite" << m_minWhite
                        << "maxWhite" << m_maxWhite
                        << "avgDelta" << avgDelta;
                // 稳定停止后再补抓一帧，确保最终帧写入 SHM（不会重新进入循环）
                if (reason == QStringLiteral("stable")) {
                    QTimer::singleShot(80, this, &WereadBrowser::captureFrame);
                    QTimer::singleShot(500, this, &WereadBrowser::captureFrame);
                }
                m_captureLoopActive = false;
                // 白度已下降但 READY 还未发现正文时，再跑一次文本扫描
                if (whiteRatio < 95.0) {
                    logTextScan(false);
                }
            } else {
                QTimer::singleShot(80, this, &WereadBrowser::captureFrame);
            }
        }

        // High-white救援：仅一次 prev/next 微翻页，防止空白卡死
        // 增加时间限制：页面加载开始后至少20秒才触发，避免干扰正常加载
        const qint64 elapsed = now - m_readyStartMs;
        if (m_enableHighWhiteRescue && m_isBookPage && !m_firstFrameDone && whiteRatio > 90.0 && elapsed >= 20000) {
            if (!m_highWhiteRescueDone) {
                m_highWhiteRescueDone = true;
                const int seq = ++m_rescueSeq;
                qWarning() << "[RESCUE] high-white seq" << seq << "trigger prev/next white%" << whiteRatio << "elapsed" << elapsed;
                goPrevPage();
                QTimer::singleShot(100, this, &WereadBrowser::goNextPage);
                // 先短延时抓帧，再兜底
                scheduleCapture(100);
                scheduleCapture(1500);
            }
        }
    }
    void onExitClicked() {
        qInfo() << "[EXIT] Exit button clicked, returning to system UI...";

        // Keep frontend running but stop backend and restart xochitl
        // This allows resuming with the same browsing state
        QProcess::startDetached("/bin/sh", QStringList() << "-c" <<
            "killall -9 epaper_shm_viewer 2>/dev/null; "
            "systemctl start xochitl");

        qInfo() << "[EXIT] Frontend will keep running in background for quick resume";
        // Do NOT quit the application - keep it running in background
    }
public:
    void injectMouse(Qt::MouseButton btn, QEvent::Type type, const QPointF& pos) {
        if (!m_view) return;
        const qreal z = m_view->zoomFactor();
        const QPointF pagePos = (z != 0.0) ? (pos / z) : pos;
        QMouseEvent *ev = new QMouseEvent(type, pagePos, btn, btn, Qt::NoModifier);
        QCoreApplication::postEvent(m_view, ev);
    }
    void scrollByJs(int dy) {
        if (!m_view || !m_view->page()) return;
        m_view->page()->runJavaScript(QStringLiteral("window.scrollBy(0,%1);").arg(dy));
        qInfo() << "[TOUCH] js scroll" << dy;
        scheduleBookCaptures();
    }
    int pageStep() const {
        if (m_view) return static_cast<int>(m_view->height() * 0.3);
        return 400;
    }
    void hittestJs(const QPointF& pos) {
        if (!m_view || !m_view->page()) return;
        // Fix coordinate scaling: divide by zoomFactor to get page coordinates
        const qreal zoomFactor = m_view->zoomFactor();
        const QPointF pagePos = pos / zoomFactor;
        qInfo() << "[HITTEST] Screen pos:" << pos << "Page pos:" << pagePos << "Zoom:" << zoomFactor;
        const QString script = QStringLiteral(
            "var x=%1,y=%2;"
            "function tag(e){if(!e) return 'null';"
            " var t=e.tagName; if(e.id) t+='#'+e.id;"
            " if(e.className) t+='.'+e.className; return t;}"
            "var el=document.elementFromPoint(x,y);"
            "var res='';"
            "if(!el){res='null';}"
            "else if(el.tagName==='IFRAME'){"
            "  res='iframe';"
            "  var r=el.getBoundingClientRect();"
            "  try{"
            "    var d=el.contentDocument||el.contentWindow.document;"
            "    if(d){"
            "      var inner=d.elementFromPoint(x-r.left,y-r.top);"
            "      res+='->'+tag(inner);"
            "      if(inner && inner.click) inner.click();"
            "    }"
            "  }catch(e){ res+='->blocked'; }"
            "}"
            "else {"
            "  var cls=(el.className||'')+'';"
            "  var txt=(el.innerText||'').trim();"
            "  res=tag(el)+\"|\"+txt;"
            "  if(!(cls.includes('btn focus-btn') && location.href.includes('/ebook/reader/'))){"
            "    if(el.click) el.click();"
            "  } else {"
            "    res+='|blocked';"
            "  }"
            "}"
            "res;"
        ).arg(pagePos.x()).arg(pagePos.y());
        m_view->page()->runJavaScript(script, [](const QVariant &v){
            qInfo() << "[HITTEST]" << v.toString();
        });
    }
    void injectWheel(const QPointF& pos, int dy) {
        if (!m_view) return;
        // Map accumulated pixel drag to wheel: 120 units per step is the Qt convention.
        // Positive dy should scroll down (natural touch).
        const int angleStep = dy; // use pixel distance as angle seed
        QPoint pixelDelta(0, -dy);                      // natural: drag down -> scroll down
        QPoint angleDelta(0, -angleStep);               // same sign as pixelDelta
        auto *ev = new QWheelEvent(pos, m_view->mapToGlobal(pos.toPoint()),
                                   pixelDelta, angleDelta, Qt::NoButton,
                                   Qt::NoModifier, Qt::ScrollUpdate, false);
        QCoreApplication::postEvent(m_view, ev);
        qInfo() << "[TOUCH] wheel dy" << dy << "pos" << pos;
    }
    void goNextPage() {
        if (!m_view || !m_view->page()) return;
        if (m_navigating) {
            qInfo() << "[PAGER] skip next: navigating";
            return;
        }
        const int currentSeq = ++m_navSequence;
        m_navigating = true;
        stopReadyWatch();
        cancelPendingCaptures();  // 取消旧页面的待执行抓帧
        m_firstFrameDone = false;
        m_reloadAttempts = 0;
        const qint64 tsStart = QDateTime::currentMSecsSinceEpoch();
        qInfo() << "[PAGER] next start ts" << tsStart << "seq" << currentSeq << "url" << m_view->url();
        if (isDedaoBook()) {
            dedaoScroll(true);
            return;
        }
        if (isWeReadBook()) {
            const bool isKindleUA = m_currentUA.contains(QStringLiteral("Kindle"), Qt::CaseInsensitive)
                                    || m_uaWeReadBook.contains(QStringLiteral("Kindle"), Qt::CaseInsensitive)
                                    || m_uaMode.compare(QStringLiteral("kindle"), Qt::CaseInsensitive) == 0;
            if (m_weReadBookMode == QStringLiteral("mobile") && !isKindleUA) {
                logPageProbe(QStringLiteral("[PAGER] weRead next probe (mobile)"));
                weReadScroll(true);
                return;
            }
            qInfo() << "[PAGER] weRead kindle/web mode: using pager buttons";
        }
        QTimer::singleShot(1000, this, [this, currentSeq](){
            if (m_navigating && m_navSequence == currentSeq) {
                m_navigating = false;
                qWarning() << "[PAGER] Force unlock next (timeout 1000ms) seq" << currentSeq;
                startReadyWatch();
            }
        });
        const QString js = QStringLiteral(
            "var btn=document.querySelector('.renderTarget_pager_button_right');"
            "if(btn&&btn.click){btn.click();true;}else{false;}"
        );
        m_view->page()->runJavaScript(js, [this, currentSeq](const QVariant &res){
            const bool ok = res.toBool();
            qInfo() << "[PAGER] next click result" << ok;
            if (!ok) {
                const int fallback = pageStep();
                qInfo() << "[PAGER] next fallback scrollBy" << fallback;
                scrollByJs(fallback);
            }
            keepAliveNearChapterEnd();
            startReadyWatch();
            if (m_navSequence == currentSeq) {
                m_navigating = false;
            } else {
                qInfo() << "[PAGER] next callback ignored, seq mismatch";
            }
            if (isWeReadBook() && m_weReadBookMode == QStringLiteral("mobile")) alignWeReadPagination();
        });
    }
    void goPrevPage() {
        if (!m_view || !m_view->page()) return;
        if (m_navigating) {
            qInfo() << "[PAGER] skip prev: navigating";
            return;
        }
        const int currentSeq = ++m_navSequence;
        m_navigating = true;
        stopReadyWatch();
        cancelPendingCaptures();  // 取消旧页面的待执行抓帧
        m_firstFrameDone = false;
        m_reloadAttempts = 0;
        const qint64 tsStart = QDateTime::currentMSecsSinceEpoch();
        qInfo() << "[PAGER] prev start ts" << tsStart << "seq" << currentSeq << "url" << m_view->url();
        if (isDedaoBook()) {
            dedaoScroll(false);
            return;
        }
        if (isWeReadBook()) {
            const bool isKindleUA = m_currentUA.contains(QStringLiteral("Kindle"), Qt::CaseInsensitive)
                                    || m_uaWeReadBook.contains(QStringLiteral("Kindle"), Qt::CaseInsensitive)
                                    || m_uaMode.compare(QStringLiteral("kindle"), Qt::CaseInsensitive) == 0;
            if (m_weReadBookMode == QStringLiteral("mobile") && !isKindleUA) {
                logPageProbe(QStringLiteral("[PAGER] weRead prev probe (mobile)"));
                weReadScroll(false);
                return;
            }
            qInfo() << "[PAGER] weRead kindle/web mode: using pager buttons";
        }
        QTimer::singleShot(1000, this, [this, currentSeq](){
            if (m_navigating && m_navSequence == currentSeq) {
                m_navigating = false;
                qWarning() << "[PAGER] Force unlock prev (timeout 1000ms) seq" << currentSeq;
                startReadyWatch();
            }
        });
        const QString js = QStringLiteral(
            "var btn=document.querySelector('.renderTarget_pager_button');"
            "if(btn&&btn.click){btn.click();true;}else{false;}"
        );
        m_view->page()->runJavaScript(js, [this, currentSeq](const QVariant &res){
            const bool ok = res.toBool();
            qInfo() << "[PAGER] prev click result" << ok;
            if (!ok) {
                const int fallback = -pageStep();
                qInfo() << "[PAGER] prev fallback scrollBy" << fallback;
                scrollByJs(fallback);
            }
            keepAliveNearChapterEnd();
            startReadyWatch();
            if (m_navSequence == currentSeq) {
                m_navigating = false;
            } else {
                qInfo() << "[PAGER] prev callback ignored, seq mismatch";
            }
            if (isWeReadBook() && m_weReadBookMode == QStringLiteral("mobile")) alignWeReadPagination();
        });
    }
    void openCatalog() {
        if (!m_view || !m_view->page()) return;
        qInfo() << "[MENU] catalog click skipped (buttons hidden)";
    }
    void adjustFont(bool increase) {
        if (!m_view || !m_view->page()) return;
        cancelPendingCaptures();  // 取消旧的待执行抓帧
        m_firstFrameDone = false;
        m_reloadAttempts = 0;
        if (isDedaoBook()) {
            const QString openSettingJs = QStringLiteral(
                "(() => {"
                "  const settingBtn = document.querySelector('.reader-tool-button.tool-margin-right');"
                "  if (settingBtn && typeof settingBtn.click === 'function') {"
                "    settingBtn.click();"
                "    return {ok:true, action:'open-setting'};"
                "  }"
                "  return {ok:false, reason:'no-setting-btn'};"
                "})();"
            );
            m_view->page()->runJavaScript(openSettingJs, [this, increase](const QVariant &res){
                qInfo() << "[MENU] open setting panel" << res;
                QTimer::singleShot(300, this, [this, increase](){
                    const QString adjustFontJs = QStringLiteral(
                        "(() => {"
                        "  const inc=%1;"
                        "  const buttons = Array.from(document.querySelectorAll('.set-tool-box-change-font button'));"
                        "  if (buttons.length === 0) return {ok:false, reason:'no-font-buttons'};"
                        "  const btn = inc>0 ? buttons[buttons.length-1] : buttons[0];"
                        "  if (btn && btn.click) {"
                        "    btn.click();"
                        "    const fontNum = document.querySelector('.set-tool-box-font-number');"
                        "    const num = fontNum ? fontNum.textContent.trim() : '';"
                        "    return {ok:true, mode:'dedao', inc, font:num};"
                        "  }"
                        "  return {ok:false, reason:'btn-no-click', mode:'dedao'};"
                        "})();"
                    ).arg(increase ? 1 : -1);
                    m_view->page()->runJavaScript(adjustFontJs, [this](const QVariant &res){
                        qInfo() << "[MENU] font step (dedao)" << res;
                        startReadyWatch();
                    });
                });
            });
            return;
        }
        const QString js = QStringLiteral(
            "(() => {"
            "  const delta=%1;"
            "  let setting={};"
            "  try{ setting=JSON.parse(localStorage.getItem('wrLocalSetting')||'{}'); }catch(e){ setting={}; }"
            "  const before=setting.fontSizeLevel||3;"
            "  const after=Math.max(1, Math.min(7, before+delta));"
            "  setting.fontSizeLevel=after;"
            "  if(!setting.fontFamily) setting.fontFamily='wr_default_font';"
            "  try{ localStorage.setItem('wrLocalSetting', JSON.stringify(setting)); }catch(e){}"
            "  setTimeout(()=>location.reload(),50);"
            "  return {ok:true,before,after};"
            "})();").arg(increase ? 1 : -1);
        m_view->page()->runJavaScript(js, [this](const QVariant &res){
            qInfo() << "[MENU] font step result" << res;
            startReadyWatch();
        });
    }
    void toggleTheme() {
        if (!m_view || !m_view->page()) return;
        cancelPendingCaptures();  // 取消旧的待执行抓帧
        m_firstFrameDone = false;
        m_reloadAttempts = 0;
        if (isDedaoBook()) {
            const QString openSettingJs = QStringLiteral(
                "(() => {"
                "  const settingBtn = document.querySelector('.reader-tool-button.tool-margin-right');"
                "  if (settingBtn && typeof settingBtn.click === 'function') {"
                "    settingBtn.click();"
                "    return {ok:true, action:'open-setting'};"
                "  }"
                "  return {ok:false, reason:'no-setting-btn'};"
                "})();"
            );
            m_view->page()->runJavaScript(openSettingJs, [this](const QVariant &res){
                qInfo() << "[MENU] open setting panel for theme" << res;
                QTimer::singleShot(300, this, [this](){
                    const QString toggleThemeJs = QStringLiteral(
                        "(() => {"
                        "  const list = Array.from(document.querySelectorAll('.tool-box-theme-group .reader-tool-theme-button'));"
                        "  if (list.length === 0) return {ok:false, reason:'no-theme-buttons'};"
                        "  const curIdx = list.findIndex(el => el.classList.contains('reader-tool-theme-button-selected'));"
                        "  const nextIdx = list.length ? ((curIdx>=0 ? (curIdx+1)%list.length : 0)) : -1;"
                        "  if (nextIdx>=0 && list[nextIdx] && list[nextIdx].click) {"
                        "    list[nextIdx].click();"
                        "    const sel = list[nextIdx].querySelector('.reader-tool-theme-button-text');"
                        "    const text = sel ? sel.textContent.trim() : '';"
                        "    return {ok:true, cur:curIdx, next:nextIdx, count:list.length, mode:'dedao', text};"
                        "  }"
                        "  return {ok:false, reason:'no-theme-btn', count:list.length, mode:'dedao'};"
                        "})();"
                    );
                    m_view->page()->runJavaScript(toggleThemeJs, [this](const QVariant &res){
                        qInfo() << "[MENU] theme toggle (dedao)" << res;
                        startReadyWatch();
                    });
                });
            });
            return;
        }
        const QString js = QStringLiteral(
            "(() => {"
            "  function getCookie(name){"
            "    return document.cookie.split(';').map(s=>s.trim()).filter(s=>s.startsWith(name+'='))"
            "      .map(s=>s.substring(name.length+1))[0] || '';"
            "  }"
            "  const cookieTheme = getCookie('wr_theme');"
            "  const lsTheme = (localStorage && localStorage.getItem('wr_theme')) || '';"
            "  const current = (cookieTheme||lsTheme||'dark').toLowerCase();"
            "  const next = current==='white' ? 'dark' : 'white';"
            "  try { document.cookie = 'wr_theme='+next+'; domain=.weread.qq.com; path=/; max-age=31536000'; } catch(e) {}"
            "  try { localStorage && localStorage.setItem('wr_theme', next); } catch(e) {}"
            "  const delay=300;"
            "  console.log('[THEME] reload scheduled in',delay,'ms');"
            "  setTimeout(()=>{ console.log('[THEME] reloading now'); location.reload(); },delay);"
            "  return {ok:true, before:current, after:next, cookieAfter:getCookie('wr_theme'), lsAfter:(localStorage&&localStorage.getItem('wr_theme'))||''};"
            "})();"
        );
        m_view->page()->runJavaScript(js, [this](const QVariant &res){
            qInfo() << "[MENU] theme toggle" << res;
            startReadyWatch();
        });
    }
    void toggleFontFamily() {
        if (!m_view || !m_view->page()) return;
        m_firstFrameDone = false;
        m_reloadAttempts = 0;
        const QString js = QStringLiteral(
            "(() => {"
            "  const key='wrLocalSetting';"
            "  let setting={};"
            "  try{ setting=JSON.parse(localStorage.getItem(key)||'{}'); }catch(e){ setting={}; }"
            "  const cur=(setting.fontFamily||'wr_default_font').toLowerCase();"
            "  const custom='custom_noto_sans';"
            "  const next = (cur===custom) ? 'wr_default_font' : custom;"
            "  setting.fontFamily = next;"
            "  if(!setting.fontSizeLevel) setting.fontSizeLevel = 3;"
            "  try{ localStorage.setItem(key, JSON.stringify(setting)); }catch(e){}"
            "  const cssId='wr-font-override';"
            "  const exist=document.getElementById(cssId);"
            "  if (exist) exist.remove();"
            "  if (next===custom){"
            "    const style=document.createElement('style');"
            "    style.id=cssId;"
            "    style.textContent = \"@font-face{font-family:'CustomNotoSans';src:url('file:///home/root/.fonts/NotoSansCJKsc-Regular.otf') format('opentype');}\"+"
            "      \".renderTargetContent, .renderTargetContent * { font-family:'CustomNotoSans', 'Noto Sans CJK SC', 'Noto Sans SC', sans-serif !important; }\";"
            "    try{ (document.documentElement||document.body).appendChild(style); }catch(e){}"
            "  }"
            "  setTimeout(()=>location.reload(), 200);"
            "  return {ok:true,next};"
            "})();"
        );
        m_view->page()->runJavaScript(js, [this](const QVariant &res){
            qInfo() << "[MENU] font toggle" << res;
            startReadyWatch();
        });
    }
    void toggleService() {
        if (!m_view || !m_view->page()) return;
        m_firstFrameDone = false;
        m_reloadAttempts = 0;
        const QString cur = m_view->url().toString();
        const bool isDedao = cur.contains(QStringLiteral("dedao.cn"));
        const QUrl next = isDedao
            ? QUrl(QStringLiteral("https://weread.qq.com/"))
            : QUrl(QStringLiteral("https://www.dedao.cn/"));
        qInfo() << "[MENU] service toggle" << (isDedao ? "dedao->weread" : "weread->dedao") << "next" << next;
        m_view->load(next);
    }
    void applyDefaultLightTheme() {
        Q_UNUSED(m_view);
    }
    void dedaoScroll(bool forward) {
        if (!m_view || !m_view->page()) { m_navigating = false; return; }

        const int currentSeq = ++m_navSequence;
        m_navigating = true;

        // 看门狗保持 1200ms
        QTimer::singleShot(1200, this, [this, currentSeq](){
            if (m_navigating && m_navSequence == currentSeq) {
                m_navigating = false;
                qWarning() << "[PAGER] Force unlock (timeout 1200ms) seq" << currentSeq;
                startReadyWatch();
            }
        });

        const int step = forward ? 1000 : -1000;
        // 【打点1】记录 C++ 发出指令的时间
        const qint64 cppSendTime = QDateTime::currentMSecsSinceEpoch();
        qInfo() << "[PAGER] dedao dispatch seq" << currentSeq << "forward" << forward << "ts" << cppSendTime;

        const QString js = QStringLiteral(
            R"(
            (() => {
                try {
                    // 【打点2】记录 JS 真正开始执行的时间
                    const jsStartTime = Date.now();
                    const step = %1;

                    function wakeUp(_el) { return; }

                    let result = {ok:false, reason:'stuck'};

                    // 1) 缓存优先
                    if (window.__SCROLL_EL && window.__SCROLL_EL.isConnected) {
                        const el = window.__SCROLL_EL;
                        const before = el.scrollTop || 0;
                        el.scrollBy(0, step);
                        const after = el.scrollTop || 0;
                        if (Math.abs(after - before) > 1) {
                            wakeUp(el);
                            result = {ok:true, mode:'cached', tag:el.tagName, delta: after-before};
                        } else {
                            window.__SCROLL_EL = null;
                        }
                    }

                    // 2) 搜索候选
                    if (!result.ok) {
                        const candidates = ['.iget-reader-book', '.reader-content', '.iget-reader-container', '.reader-body', 'html', 'body'];
                        for (const sel of candidates) {
                            const el = document.querySelector(sel);
                            if (!el || !el.isConnected) continue;
                            const before = el.scrollTop || 0;
                            el.scrollBy(0, step);
                            const after = el.scrollTop || 0;
                            if (Math.abs(after - before) > 1) {
                                window.__SCROLL_EL = el;
                                wakeUp(el);
                                result = {ok:true, mode:'search', tag:el.tagName, delta: after-before};
                                break;
                            }
                        }
                    }

                    // 3) 兜底 Window
                    if (!result.ok) {
                        const bWin = window.scrollY;
                        window.scrollBy(0, step);
                        if (Math.abs(window.scrollY - bWin) > 0) {
                            result = {ok:true, mode:'window', delta:(window.scrollY - bWin)};
                        }
                    }

                    // 【打点3】记录 JS 执行结束的时间
                    const jsEndTime = Date.now();
                    result.t_js_start = jsStartTime;
                    result.t_js_end   = jsEndTime;
                    return result;

                } catch(e) { return {ok:false, error:String(e)}; }
            })()
            )"
        ).arg(step);

        m_view->page()->runJavaScript(js, [this, cppSendTime, currentSeq](const QVariant &res){
            // 【打点4】记录 C++ 收到回调的时间
            const qint64 cppRecvTime = QDateTime::currentMSecsSinceEpoch();

            const QVariantMap m = res.toMap();
            const qint64 jsStartTime = m.value("t_js_start").toLongLong();
            const qint64 jsEndTime   = m.value("t_js_end").toLongLong();

            const qint64 queueDelay   = jsStartTime - cppSendTime;       // 排队耗时
            const qint64 execTime     = jsEndTime   - jsStartTime;       // JS执行耗时
            const qint64 ipcDelay     = cppRecvTime - jsEndTime;         // 回传耗时
            const qint64 totalElapsed = cppRecvTime - cppSendTime;

            const int delta = m.value("delta").toInt();
            const QString mode = m.value("mode").toString();

            if (totalElapsed > 800) {
                qWarning().noquote() << QString("[PAGER] SLOW | Total: %1ms | Queue: %2ms | Exec: %3ms | IPC: %4ms | Mode: %5 | Delta: %6")
                                        .arg(totalElapsed).arg(queueDelay).arg(execTime).arg(ipcDelay).arg(mode).arg(delta);
            } else {
                qInfo().noquote() << QString("[PAGER] OK   | Total: %1ms | Queue: %2ms | Exec: %3ms | Mode: %4 | Delta: %5")
                                     .arg(totalElapsed).arg(queueDelay).arg(execTime).arg(mode).arg(delta);
            }

            QTimer::singleShot(200, this, [this](){ startReadyWatch(); });

            if (m_navSequence == currentSeq) {
                m_navigating = false;
            } else {
                qInfo() << "[PAGER] dedao callback ignored, seq mismatch";
            }
        });
    }
    void goBack() {
        if (!m_view || !m_view->page()) return;
        auto *hist = m_view->page()->history();
        const bool canBack = hist && hist->canGoBack();
        const int count = hist ? hist->count() : -1;
        const int idx = hist ? hist->currentItemIndex() : -1;
        const QString currentUrl = m_view->url().toString();
        const bool inReader = currentUrl.contains(QStringLiteral("/web/reader/"));
        const bool isDedaoReader = isDedaoBook();

        qInfo() << "[BACK] canGoBack" << canBack << "count" << count
                << "currentIndex" << idx
                << "url" << m_view->url();

        // Dedao 特例：直接跳转详情页，避免空 history.back
        if (isDedaoReader) {
            const QString cur = m_view->url().toString();
            QString detail = cur;
            const int pos = cur.indexOf(QStringLiteral("/ebook/reader"));
            if (pos >= 0) {
                detail = cur;
                detail.replace(QStringLiteral("/ebook/reader"), QStringLiteral("/ebook/detail"));
            }
            qInfo() << "[BACK] dedao redirect to detail" << detail;
            m_allowDedaoDetailOnce = true; // 允许一次详情跳转
            m_view->setUrl(QUrl(detail));
            return;
        }

        if (canBack) {
            // Use JS history.back so SPA can handle its own stack; then log the result a moment later.
            m_view->page()->runJavaScript(QStringLiteral("window.history.back();"));
            QTimer::singleShot(800, this, [this]() {
                auto *h = m_view->page()->history();
                const int afterCount = h ? h->count() : -1;
                const int afterIdx = h ? h->currentItemIndex() : -1;
                qInfo() << "[BACK] after-back url" << m_view->url()
                        << "count" << afterCount << "currentIndex" << afterIdx;
            });
        } else if (inReader) {
            // Fallback: When browser history is empty (due to redirects),
            // navigate back to the main weread page instead of exiting
            qInfo() << "[BACK] history empty in reader, fallback to weread home";
            m_view->setUrl(QUrl(QStringLiteral("https://weread.qq.com/")));
        } else {
            // We're already at a top-level page (weread home), exit the app
            qInfo() << "[BACK] no history available, exiting";
            exitToXochitl();
        }
    }
    void allowDedaoDetailOnce() { m_allowDedaoDetailOnce = true; }
    void showMenu() {
        if (!m_menu) return;
        // stretch to top with margin to ensure visibility
        const int w = m_view ? m_view->width() : (this->width() > 0 ? this->width() : 954);
        const int x = 10;
        const int y = 70;   // further down by 10px
        const int h = 100;
        const int menuWidth = qMax(100, w - 2 * x); // full width, no right reserve
        m_menu->setGeometry(x, y, menuWidth, h);
        m_menu->setVisible(true);
        m_menu->raise();
        qInfo() << "[MENU] show overlay";
        m_menuTimer.start();
        QTimer::singleShot(5000, this, [this](){ captureFrame(); });
        QTimer::singleShot(6000, this, [this](){ captureFrame(); });
        runDomDiagnostic();
        startReadyWatch();
    }
    void exitToXochitl() {
        QProcess::startDetached(QStringLiteral("/home/root/weread/exit-wechatread.sh"));
    }
    void sendBookState() {
        const bool isBook = isWeReadBook() || isDedaoBook();
        QByteArray b(1, isBook ? '\x01' : '\x00');
        m_stateSender.writeDatagram(b, QHostAddress(QStringLiteral("127.0.0.1")), 45456);
        qInfo() << "[STATE] isBookPage" << isBook << "url" << currentUrl;
        const bool wasBook = m_prevIsBookPage;
        m_prevIsBookPage = isBook;
        m_isBookPage = isBook;
        // 动态调整缩放：非书籍页放大到 2.0，书籍页保持 1.6
        if (m_view) {
            const qreal targetZoom = isBook ? 1.6 : 2.0;
            if (!qFuzzyCompare(m_view->zoomFactor(), targetZoom)) {
                m_view->setZoomFactor(targetZoom);
                qInfo() << "[ZOOM] set zoom" << targetZoom << "isBook" << isBook;
            }
        }
        if (isBook && !wasBook) {
            cancelPendingCaptures();  // 取消之前页面的待执行抓帧
            m_firstFrameDone = false;
            m_reloadAttempts = 0;
            m_highWhiteRescueDone = false;
            m_bannerHidden = false;
            m_dedaoProbed = false;
            m_emptyReadyPolls = 0;
            m_stallRescueTriggered = false;
            m_diagRan = false;
            m_scriptInventoryLogged = false;
            m_resourceLogged = false;
            m_textScanDone = false;
            m_rescueSeq = 0;
            m_iframeFixApplied = false;
            m_lowPowerCssInjected = false;
            QTimer::singleShot(500, this, [this](){ logScriptInventory(); });
            QTimer::singleShot(1000, this, [this](){ logResourceEntries(); });
            QTimer::singleShot(30000, this, [this](){ logScriptInventory(false); logResourceEntries(false); });
            QTimer::singleShot(60000, this, [this](){ logScriptInventory(false); logResourceEntries(false); logTextScan(false); });
            QTimer::singleShot(120000, this, [this](){ logScriptInventory(false); logResourceEntries(false); logTextScan(false); });
            QTimer::singleShot(150000, this, [this](){ logScriptInventory(false); logResourceEntries(false); logTextScan(false); });
        }
        if (!isBook) m_scriptInventoryLogged = false;
        updateHeartbeat();
        if (isBook && !wasBook) {
            startReadyWatch();
        }
    }
    void handleStateRequest() {
        while (m_stateResponder.hasPendingDatagrams()) {
            QByteArray d;
            d.resize(int(m_stateResponder.pendingDatagramSize()));
            QHostAddress sender;
            quint16 port = 0;
            m_stateResponder.readDatagram(d.data(), d.size(), &sender, &port);
            Q_UNUSED(d);
            sendBookState(); // reply by sending current state to 45456 as usual
            qInfo() << "[STATE] request received from" << sender.toString() << "port" << port;
        }
    }
private:
    QWebEngineView* m_view = nullptr;
    QTimer* m_captureTimer = nullptr;
    ShmFrameWriter* m_writer = nullptr;
    QPushButton* m_exitButton = nullptr;
    QPushButton* m_uaButton = nullptr;
    QWebEngineProfile* m_profile = nullptr;
    // UA 控制：微信书籍页用 Qt 默认 UA，其余用配置 UA
    QString m_uaNonWeRead;
    QString m_uaWeReadBook;
    QString m_kindleUA;
    QString m_ipadMiniUA;
    QString m_currentUA;
    QString m_weReadBookMode;
    QString m_uaMode = QStringLiteral("default");
    QString detectUaMode(const QString &ua) const {
        const QString low = ua.toLower();
        if (low.contains(QStringLiteral("ipad"))) return QStringLiteral("ipad");
        if (low.contains(QStringLiteral("kindle"))) return QStringLiteral("kindle");
        if (low.contains(QStringLiteral("android"))) return QStringLiteral("android");
        return QStringLiteral("default");
    }
    QFrame* m_menu = nullptr;
    QTimer m_menuTimer;
    QTimer m_heartbeatTimer;
    QTimer m_readyTimer;
    QVector<QTimer*> m_pendingCaptureTimers;
    int m_readyPollCount = 0;
    bool m_readyDisabled = false; // READY 默认开启；可通过标志临时关闭
    bool m_disablePrep = true;    // 关闭首帧捕获/样式准备
    bool m_disableRescue = true;  // 关闭救援/兜底
    int m_emptyReadyPolls = 0;
    bool m_stallRescueTriggered = false;
    bool m_firstFrameDone = false;
    int m_reloadAttempts = 0;
    int m_integrityReloads = 0;
    int m_bookEarlyReloads = 0;
    bool m_stylesOk = false;
    bool m_enableHighWhiteRescue = false; // 可开关的高白救援（默认暂停）
    bool m_highWhiteRescueDone = false;
    bool m_bannerHidden = false;
    bool m_diagRan = false;
    bool m_scriptInventoryLogged = false;
    bool m_resourceLogged = false;
    bool m_textScanDone = false;
    int m_rescueSeq = 0;
    bool m_iframeFixApplied = false;
    bool m_lowPowerCssInjected = false;
    bool m_allowDedaoDetailOnce = false;
    QUdpSocket m_stateSender;
    QUdpSocket m_stateResponder;
    QUrl currentUrl;
    QString m_prevUrlStr;
    QString m_lastDedaoReaderUrl;
    bool m_isBookPage = false;
    bool m_prevIsBookPage = false;
    qint64 m_lastReadyTs = 0;
    qint64 m_readyStartMs = 0;
    qint64 m_lastReloadTs = 0;
    qint64 m_bookEnterTs = 0;
    bool m_dedaoProbed = false;
    bool m_navigating = false; // 翻页防抖锁
    int m_navSequence = 0;     // 翻页序列号，防止僵尸回调
    // 捕帧循环控制
    bool m_captureLoopActive = false;
    qint64 m_captureLoopStart = 0;
    int m_captureFrameCount = 0;
    double m_lastWhite = -1.0;
    int m_stableCount = 0;
    qint64 m_lastTouchTs = 0;
    double m_firstWhite = -1.0;
    double m_minWhite = 1e9;
    double m_maxWhite = -1.0;
    qint64 m_captureDeltaSum = 0;
    int m_captureDeltaCount = 0;
    qint64 m_lastCaptureTs = 0;
    int m_pageTurnCaptureSeq = 0;
public:
    bool isWeReadBook() const {
        const QString u = currentUrl.toString();
        // 放宽匹配：只要域是 weread.qq.com 且路径包含 /web/reader 即视为书籍页
        return u.contains(QStringLiteral("weread.qq.com")) &&
               u.contains(QStringLiteral("/web/reader"));
    }
    bool isDedaoBook() const {
        const QString u = currentUrl.toString();
        return u.contains(QStringLiteral("dedao.cn")) && u.contains(QStringLiteral("/ebook/reader"));
    }
    QWebEngineView* getView() const { return m_view; }
    void stopReadyWatch() {
        m_readyTimer.stop();
        m_firstFrameDone = false;
        m_readyPollCount = 0;
        m_emptyReadyPolls = 0;
        m_stallRescueTriggered = false;
    }
    void restartReadyWatch() {
        if (m_readyDisabled) return;
        stopReadyWatch();
        startReadyWatch();
    }
    void forceRepaintNudge() {
        if (!m_view || !m_view->page()) return;
        const QString js = QStringLiteral(
            "(() => {"
            "  try {"
            "    window.scrollBy(0,1);"
            "    setTimeout(()=>window.scrollBy(0,-1),50);"
            "    const b=document.body; if(b){ const op=b.style.opacity; b.style.opacity='0.999'; setTimeout(()=>{b.style.opacity=op||'';},50); }"
            "    return {ok:true};"
            "  } catch(e) { return {ok:false, error:String(e)}; }"
            "})();"
        );
        m_view->page()->runJavaScript(js, [](const QVariant &res){
            qInfo() << "[REPAINT_NUDGE]" << res;
        });
    }
    // 根据 URL 切换 UA：微信书籍页用 Qt 默认 UA，其余用配置的 iOS/Kindle/Android UA
    void updateUserAgentForUrl(const QUrl &url) {
        if (!m_profile) return;
        const QString u = url.toString();
        const bool weReadBook = u.contains(QStringLiteral("weread.qq.com")) &&
                                u.contains(QStringLiteral("/web/reader/"));
        const bool dedaoBook = u.contains(QStringLiteral("dedao.cn")) &&
                               u.contains(QStringLiteral("/ebook/reader"));
        const QString targetUA = dedaoBook ? (m_kindleUA.isEmpty() ? m_uaNonWeRead : m_kindleUA)
                                            : (weReadBook ? m_uaWeReadBook : m_uaNonWeRead);
        if (targetUA == m_currentUA) {
            // 仍然更新按钮，防止模式和显示不同步
            m_uaMode = detectUaMode(m_currentUA);
            updateUaButtonText(m_uaMode);
            return;
        }
        m_profile->setHttpUserAgent(targetUA);
        m_currentUA = targetUA;
        m_uaMode = detectUaMode(m_currentUA);
        qInfo() << "[UA] switched" << (weReadBook ? "weread-book" : "non-weread") << targetUA;
        updateUaButtonText(m_uaMode);
    }
    void ensureDedaoDefaults() {
        if (!isDedaoBook() || !m_view || !m_view->page()) return;
        // 强制 Kindle UA（需求：进入得到书籍页默认 UA 为 Kindle）
        if (!m_kindleUA.isEmpty() && m_profile && m_profile->httpUserAgent() != m_kindleUA) {
            m_profile->setHttpUserAgent(m_kindleUA);
            m_currentUA = m_kindleUA;
            m_uaMode = detectUaMode(m_currentUA);
            updateUaButtonText(m_uaMode);
            qInfo() << "[DEDAO] UA forced to kindle";
        }
        // 缩放：2.0
        const qreal targetZoom = 2.0;
        if (!qFuzzyCompare(m_view->zoomFactor(), targetZoom)) {
            m_view->setZoomFactor(targetZoom);
            qInfo() << "[DEDAO] zoom set" << targetZoom;
        }
    }
    void updateLastTouchTs(qint64 ts) { m_lastTouchTs = ts; }
    void openDedaoMenu() {
        if (!isDedaoBook() || !m_view || !m_view->page()) return;
        const QString js = R"(
            (function() {
                try {
                    window.scrollTo(0,0);
                    const toolbar = document.querySelector('.iget-reader-toolbar');
                    const btn = Array.from(toolbar ? toolbar.querySelectorAll('button,[role=button]') : [])
                        .find(b => ((b.innerText||'').includes('设置')) || (b.className||'').includes('iget-icon-font'));
                    if (btn && btn.click) {
                        btn.click();
                        setTimeout(() => {
                            const panel = document.querySelector('.set-tool-box');
                            if (panel) {
                                panel.style.cssText += '; display:block !important; visibility:visible !important; opacity:1 !important;'
                                    + ' z-index:99999 !important; position:fixed !important; top:15% !important; left:10% !important; width:80% !important;'
                                    + ' min-width:300px !important; min-height:150px !important; background:#fff !important; border:2px solid #000 !important;';
                                console.log('[MENU_FIX] Styles forced');
                            }
                        }, 50);
                        return {ok:true, action:'clicked'};
                    }
                    return {ok:false, reason:'no-btn'};
                } catch(e) { return {ok:false, error:String(e)}; }
            })()
        )";
        m_view->page()->runJavaScript(js, [](const QVariant &res){
            qInfo() << "[MENU] dedao open" << res;
        });
    }
    void scheduleCapture(int delayMs = 100) {
        QTimer::singleShot(delayMs, this, &WereadBrowser::captureFrame);
    }
    void scheduleBookCaptures(bool force = false) {
        if (m_captureLoopActive && !force) return;
        m_captureLoopActive = true;
        m_captureLoopStart = QDateTime::currentMSecsSinceEpoch();
        m_captureFrameCount = 0;
        m_lastWhite = -1.0;
        m_stableCount = 0;
        m_firstWhite = -1.0;
        m_minWhite = 1e9;
        m_maxWhite = -1.0;
        m_captureDeltaSum = 0;
        m_captureDeltaCount = 0;
        m_lastCaptureTs = 0;
        qInfo() << "[CAP] loop start ts" << m_captureLoopStart;
        // 添加初始延迟（200ms），确保页面有足够时间完成初始渲染
        // Qt WebEngine是异步渲染的，loadFinished只表示HTML加载完成，不保证视觉渲染完成
        QTimer::singleShot(200, this, &WereadBrowser::captureFrame);
    }
    void schedulePageTurnCaptures() {
        // 翻页后使用固定 6 帧（100/500/1000/1500/2000/3000ms），避免 80ms 循环
        cancelPendingCaptures();
        m_captureLoopActive = false; // 关闭循环抓帧，改用固定定时
        const int seq = ++m_pageTurnCaptureSeq;
        if (!isWeReadBook() && !isDedaoBook()) {
            // 非书籍页维持原有循环策略
            restartCaptureLoop();
            return;
        }
        const QList<int> delays = {100, 500, 1000, 1500, 2000, 3000};
        qInfo() << "[CAP] page-turn fixed schedule" << delays;
        for (int d : delays) {
            QTimer *timer = new QTimer(this);
            timer->setSingleShot(true);
            connect(timer, &QTimer::timeout, this, [this, timer, d, seq](){
                if (seq != m_pageTurnCaptureSeq) {
                    timer->deleteLater();
                    return;
                }
                qInfo() << "[CAP] page-turn shot" << d << "ms";
                captureFrame();
                m_pendingCaptureTimers.removeOne(timer);
                timer->deleteLater();
            });
            m_pendingCaptureTimers.append(timer);
            timer->start(d);
        }
    }
    void restartCaptureLoop() {
        m_captureLoopActive = false;
        scheduleBookCaptures(true);
    }
    // 仅针对微信读书书籍页：对齐滚动到行高整数倍并添加轻微留白，避免半截行
    void alignWeReadPagination() {
        if (!isWeReadBook() || m_weReadBookMode != QStringLiteral("mobile") || !m_view || !m_view->page()) return;
        const QString js = QStringLiteral(
            "(() => {"
            "  const el = document.querySelector('.renderTargetContent') || document.scrollingElement || document.documentElement;"
            "  if (!el) return {ok:false, reason:'no-container'};"
            "  try {"
            "    // el.style.padding = '20px 35px'; // 临时禁用：排查布局/隐藏问题"
            "    el.style.boxSizing = 'border-box';"
            "    el.style.width = '100%';"
            "    el.style.maxWidth = '100%';"
            "  } catch(e) {}"
            "  const sample = el.querySelector('p, div, span');"
            "  const lh = sample ? parseFloat(getComputedStyle(sample).lineHeight) || 32 : 32;"
            "  const padTop = parseFloat(getComputedStyle(el).paddingTop) || 0;"
            "  const cur = el.scrollTop;"
            "  const aligned = Math.max(0, Math.round((cur - padTop) / lh) * lh + padTop);"
            "  if (Math.abs(aligned - cur) > 0.5) el.scrollTo({ top: aligned, behavior: 'auto' });"
            "  return {ok:true, from:cur, to:aligned, lineHeight:lh};"
            "})()"
        );
        m_view->page()->runJavaScript(js, [](const QVariant &res){
            qInfo() << "[PAGER] align weRead" << res;
        });
    }
    // 【视觉诊断】正文有字但不显示时，采集容器的可见性/尺寸/遮挡信息
    void runVisualDiagnostic() {
        if (!m_view || !m_view->page()) return;
        const QString js = QStringLiteral(
            "(() => {"
            "  try {"
            "    const target = document.querySelector('.renderTargetContent') || document.querySelector('.readerChapterContent') || document.body;"
            "    if (!target) return {error: 'target-not-found'};"
            "    const style = window.getComputedStyle(target);"
            "    const rect = target.getBoundingClientRect();"
            "    const mask = document.querySelector('.loading, .mask, .reader_loading');"
            "    let maskInfo = 'none';"
            "    if (mask) {"
            "      const ms = window.getComputedStyle(mask);"
            "      if (ms.display !== 'none' && ms.visibility !== 'hidden' && ms.opacity !== '0') {"
            "        maskInfo = {cls: mask.className, zIndex: ms.zIndex, bg: ms.backgroundColor, disp: ms.display, vis: ms.visibility, op: ms.opacity};"
            "      }"
            "    }"
            "    const iframeInfo = (()=>{"
            "      const frames = Array.from(document.querySelectorAll('iframe'));"
            "      const samples = [];"
            "      frames.forEach((f,idx)=>{"
            "        if (samples.length>=3) return;"
            "        const r = f.getBoundingClientRect();"
            "        const entry = {idx, src:(f.src||'').slice(0,120), rect:{x:r.x,y:r.y,w:r.width,h:r.height}};"
            "        try {"
            "          const d = f.contentDocument || (f.contentWindow && f.contentWindow.document);"
            "          if (d && d.body && d.body.innerText) entry.textLen = d.body.innerText.length;"
            "        } catch(_) { entry.blocked = true; }"
            "        samples.push(entry);"
            "      });"
            "      return {count:frames.length, samples};"
            "    })();"
            "    return {"
            "      tag: target.tagName,"
            "      cls: target.className,"
            "      rect: {x: rect.x, y: rect.y, w: rect.width, h: rect.height},"
            "      style: {display: style.display, visibility: style.visibility, opacity: style.opacity, zIndex: style.zIndex, position: style.position, color: style.color, bgColor: style.backgroundColor, font: style.fontFamily},"
            "      mask: maskInfo,"
            "      textLen: (target.innerText||'').length,"
            "      iframe: iframeInfo"
            "    };"
            "  } catch(e) { return {error: String(e)}; }"
            "})()"
        );
        m_view->page()->runJavaScript(js, [](const QVariant &res){
            qWarning().noquote() << "[VISUAL_DIAG]" << res;
        });
    }
private:
    void updateHeartbeat() {
        Q_UNUSED(m_isBookPage);
        // heartbeat captures disabled; keep stub to avoid timer reuse
        m_heartbeatTimer.stop();
    }
    void cancelPendingCaptures() {
        for (QTimer *timer : m_pendingCaptureTimers) {
            if (timer) {
                timer->stop();
                timer->deleteLater();
            }
        }
        m_pendingCaptureTimers.clear();
    }
    void startReadyWatch() {
        if (qEnvironmentVariableIsSet("WEREAD_START_BLANK")) {
            qInfo() << "[READY] skip in startBlank mode";
            return;
        }
        m_readyStartMs = QDateTime::currentMSecsSinceEpoch();
        m_readyPollCount = 0;
        qInfo() << "[READY] start ts" << m_readyStartMs;
        pollReady();
    }
    void pollReady() {
        if (!m_view || !m_view->page()) return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        m_readyPollCount++;
        const qint64 timeoutMs = 800;
        const qint64 hardLimitMs = 60000; // 优化：从 180s 改为 60s，加快文本出现速度
        const bool isBook = m_isBookPage;
        const QString js = QStringLiteral(
            "(() => {"
            "  try {"
            "    const isBook = %1;"
            "    const isDedao = (location.host || '').includes('dedao.cn') && (location.href || '').includes('/ebook/reader');"
            "    const isError = location.href.startsWith('chrome-error://') || (document.body && document.body.classList.contains('neterror'));"
            "    if (!window.__CHECK_COUNT) window.__CHECK_COUNT = 0;"
            "    window.__CHECK_COUNT++;"
            "    if (window.__LAST_READY && !isError && document.body) {"
            "      if (window.__CHECK_COUNT % 5 !== 0) {"
            "        return {ready:true, hasCore:true, fastPath:true, ts:Date.now()};"
            "      }"
            "    }"
            "    if (isDedao && document.body && !window.__KICK_STARTED) {"
            "      window.__KICK_STARTED = true;"
            "      window.scrollBy(0,1);"
            "      setTimeout(() => window.scrollBy(0,-1), 100);"
            "    }"
            "    let hasCore = false;"
            "    let hasText = false;"
            "    const bodyLen = (document.body && document.body.innerText) ? document.body.innerText.length : 0;"
            "    const iframeStats = (()=>{"
            "      const frames = Array.from(document.querySelectorAll('iframe'));"
            "      let text = 0, blocked = 0;"
            "      const samples = [];"
            "      frames.forEach((f,idx)=>{"
            "        const rect = f.getBoundingClientRect();"
            "        const info = {idx, src:(f.src||'').slice(0,120), rect:{x:rect.x,y:rect.y,w:rect.width,h:rect.height}};"
            "        try {"
            "          const d = f.contentDocument || (f.contentWindow && f.contentWindow.document);"
            "          if (d && d.body && d.body.innerText) {"
            "            const len = d.body.innerText.length;"
            "            text += len;"
            "            info.textLen = len;"
            "          }"
            "        } catch(_) { blocked++; info.blocked = true; }"
            "        if (samples.length < 3) samples.push(info);"
            "      });"
            "      return {count: frames.length, text, blocked, samples};"
            "    })();"
            "    const chapterTextLen = (()=>{"
            "      const el = document.querySelector('.readerChapterContent') || document.querySelector('.renderTargetContent');"
            "      if (!el) return 0;"
            "      const t = (el.innerText || el.textContent || '').trim();"
            "      return t.length;"
            "    })();"
            "    hasText = (chapterTextLen > 100) || (bodyLen > 500) || (iframeStats.text > 300);"
            "    if (!isBook) {"
            "      hasCore = !!document.body;"
            "    } else if (isDedao) {"
            "      const hasBody = !!document.body;"
            "      const hasDom = !!document.querySelector('.iget-reader-footer-pages') || !!document.querySelector('.reader-jump-page')"
            "                    || !!document.querySelector('.reader-content') || !!document.querySelector('.reader-container');"
            "      const hasText = (document.body && (document.body.innerText||'').trim().length > 0);"
            "      hasCore = hasBody && (hasDom || hasText || window.__KICK_STARTED);"
            "    } else {"
            "      const cont = document.querySelector('.renderTargetContent');"
            "      const iframes = document.querySelectorAll('iframe');"
            "      hasCore = (hasText && (!!cont || iframes.length>0)) || (iframeStats.text > 300);"
            "    }"
            "    if (hasCore && !isError) window.__LAST_READY = true;"
            "    const ts = Date.now();"
            "    const scrollEl = document.scrollingElement || document.documentElement || document.body; const scrollH = scrollEl ? scrollEl.scrollHeight : 0; const clientH = scrollEl ? scrollEl.clientHeight : 0;"
            "    return {ready: hasCore && !isError, hasCore, hasText, isError, ts, scrollH, clientH, bodyLen, chapterTextLen, iframeText: iframeStats.text, iframeCount: iframeStats.count, iframeBlocked: iframeStats.blocked, iframeSamples: iframeStats.samples};"
            "  } catch(e) { return {ready:false, error:String(e)}; }"
            "})();"
        ).arg(isBook ? QStringLiteral("true") : QStringLiteral("false"));
        m_view->page()->runJavaScript(js, [this, now, timeoutMs, hardLimitMs](const QVariant &res){
            const QVariantMap m = res.toMap();
            const bool ready = m.value(QStringLiteral("ready")).toBool();
            const bool hasCore = m.value(QStringLiteral("hasCore")).toBool();
            bool hasText = m.value(QStringLiteral("hasText")).toBool();
            const bool isError = m.value(QStringLiteral("isError")).toBool();
            const qint64 ts = m.value(QStringLiteral("ts")).toLongLong();
            const int chapterLen = m.value(QStringLiteral("chapterTextLen")).toInt();
            const int bodyLen = m.value(QStringLiteral("bodyLen")).toInt();
            const int iframeText = m.value(QStringLiteral("iframeText")).toInt();
            const int iframeCount = m.value(QStringLiteral("iframeCount")).toInt();
            const int iframeBlocked = m.value(QStringLiteral("iframeBlocked")).toInt();
            // 视觉兜底：CAP 白度已低于 94% 时，即便文本计数为 0 也视为已有内容（微信页面常见 iframe 文本不可见/高度 0）
            if (m_isBookPage && !hasText && m_lastWhite >= 0.0 && m_lastWhite < 94.0) {
                hasText = true;
                qWarning() << "[READY] fallback by CAP white" << m_lastWhite << "body/chapter/iframe text len zero";
            }
            qInfo() << "[READY] poll ready" << ready
                    << "hasCore" << hasCore
                    << "hasText" << hasText
                    << "isError" << isError
                    << "pollCount" << m_readyPollCount
                    << "elapsed" << (now - m_readyStartMs)
                    << "scrollH" << m.value(QStringLiteral("scrollH")).toInt()
                    << "clientH" << m.value(QStringLiteral("clientH")).toInt()
                    << "bodyLen" << bodyLen
                    << "chapterLen" << chapterLen
                    << "iframeText" << iframeText
                    << "iframeCount" << iframeCount
                    << "iframeBlocked" << iframeBlocked
                    << "reloadAttempts" << m_reloadAttempts;
            // 仅采集 iframe 内 body 的尺寸/滚动高度做参考，不参与判定
            if (m_isBookPage) {
                const QString iframeJs = QStringLiteral(
                    "(() => {"
                    "  try {"
                    "    const f = document.querySelector('iframe');"
                    "    if (!f) return {found:false};"
                    "    const d = f.contentDocument || (f.contentWindow && f.contentWindow.document);"
                    "    if (!d || !d.body) return {found:false, reason:'no_body'};"
                    "    const r = d.body.getBoundingClientRect();"
                    "    return {found:true, src:(f.src||'').slice(0,120), rect:{x:r.x,y:r.y,w:r.width,h:r.height}, scrollH:d.body.scrollHeight, textLen:(d.body.innerText||'').length};"
                    "  } catch(e) { return {error:String(e)}; }"
                    "})()"
                );
                m_view->page()->runJavaScript(iframeJs, [](const QVariant &r){
                    qWarning().noquote() << "[IFRAME_RECT]" << r;
                });
            }

            // 关闭兜底/救援：不做空章节延迟和翻页/重载
            // 有正文时立即跑一次视觉诊断（仅一次），便于定位不可见问题
            if (hasText && !m_diagRan) {
                m_diagRan = true;
                runVisualDiagnostic();
                logTextScan(); // when文本检测到，立即扫描一次
            }
            // 书籍页：正文探测到但疑似不可见时，重复施加容器/iframe 强制样式
            if (hasText && isWeReadBook()) {
                const bool shouldForce = (!m_iframeFixApplied) || (m_readyPollCount % 4 == 0);
                if (shouldForce) {
                    m_iframeFixApplied = true;
                    const QString fixJs = QStringLiteral(
                        "(() => {"
                        "  try {"
                        "    const cont = document.querySelector('.renderTargetContent');"
                        "    if (cont) {"
                        "      cont.style.minHeight = '100vh';"
                        "      cont.style.width = '100%';"
                        "      cont.style.display = 'block';"
                        "      cont.style.opacity = '1';"
                        "      cont.style.visibility = 'visible';"
                        "      cont.style.background = '#fff';"
                        "    }"
                        "    const iframes = Array.from(document.querySelectorAll('iframe'));"
                        "    iframes.forEach(f => {"
                        "      f.style.display = 'block';"
                        "      f.style.minHeight = '100vh';"
                        "      f.style.width = '100%';"
                        "      try {"
                        "        const d = f.contentDocument || (f.contentWindow && f.contentWindow.document);"
                        "        if (d && d.body) {"
                        "          d.body.style.background = '#fff';"
                        "          d.body.style.color = '#000';"
                        "          d.body.style.opacity = '1';"
                        "          d.documentElement && (d.documentElement.style.background = '#fff');"
                        "        }"
                        "      } catch(_) {}"
                        "    });"
                        "    window.scrollBy(0,1); setTimeout(()=>window.scrollBy(0,-1),30);"
                        "    return {ok:true, iframes: iframes.length};"
                        "  } catch(e){ return {ok:false, error:String(e)};}"
                        "})()"
                    );
                    m_view->page()->runJavaScript(fixJs, [](const QVariant &res){
                        qInfo() << "[STYLE] iframe fix" << res;
                    });
                }
            }
            // 注入低功耗样式：禁用动画/过渡/平滑滚动（只在书籍页且仅一次）
            if (ready && isWeReadBook() && !m_lowPowerCssInjected) {
                m_lowPowerCssInjected = true;
                const QString lowPowerCss = QStringLiteral(
                    "(() => {"
                    "  try {"
                    "    if (document.getElementById('weread-low-power-style')) return {ok:true, existed:true};"
                    "    const style = document.createElement('style');"
                    "    style.id = 'weread-low-power-style';"
                    "    style.textContent = `"
                    "      * {"
                    "        animation-duration: 0.001s !important;"
                    "        animation-iteration-count: 1 !important;"
                    "        transition-duration: 0s !important;"
                    "        scroll-behavior: auto !important;"
                    "      }"
                    "      html, body, .readerChapterContent, .renderTargetContent, .readerContent {"
                    "        scroll-behavior: auto !important;"
                    "        transition: none !important;"
                    "        animation: none !important;"
                    "      }"
                    "      .loading, .mask, .reader_loading {"
                    "        transition: none !important;"
                    "        animation: none !important;"
                    "      }"
                    "    `;"
                    "    document.head.appendChild(style);"
                    "    return {ok:true, injected:true};"
                    "  } catch(e) { return {ok:false, error:String(e)}; }"
                    "})()"
                );
                m_view->page()->runJavaScript(lowPowerCss, [](const QVariant &res){
                    qInfo() << "[STYLE] low-power css" << res;
                });
            }
            if (ready && ts > m_lastReadyTs) {
                m_lastReadyTs = ts;
                // 准备首帧相关动作关闭，只记录 ready
                qInfo() << "[READY] ready detected, prep disabled";
                return;
            }
            // 关闭书页的自动 reload 看门狗，避免正常加载被打断
        if (now - m_readyStartMs > hardLimitMs) {
            qWarning() << "[READY] hard limit reached, stop polling";
            return;
        }
        m_readyTimer.start(timeoutMs);
        });
    }
    qint64 m_lastKeepaliveNoStateLogMs = 0;
    void keepAliveNearChapterEnd() {
        if (!m_view || !m_view->page()) return;
        const QString js = QStringLiteral(
            "(() => {"
            "  try {"
            "    const state = (window.__INITIAL_STATE__ && window.__INITIAL_STATE__.reader) || "
            "                  (window.__NUXT__ && window.__NUXT__.state && window.__NUXT__.state.reader) || null;"
            "    const obs = window.__WR_CHAPTER_OBS || {};"
            "    let source = '';"
            "    let bookId = '';"
            "    let idx = -1;"
            "    let total = 0;"
            "    let uid = null;"
            "    if (state && state.chapterList && state.chapterList.length && state.chapterUid) {"
            "      source = 'state';"
            "      const list = state.chapterList || [];"
            "      uid = state.chapterUid;"
            "      idx = list.findIndex(c => c && (c.chapterUid === uid || c.chapter_uid === uid || String(c.chapterUid) === String(uid)));"
            "      total = list.length || 0;"
            "      bookId = state.bookId || state.book_id || '';"
            "    } else if (obs && (obs.bookId || obs.chapterUid || obs.total || obs.idx!==undefined || obs.localIdx!==undefined)) {"
            "      source = 'observer';"
            "      bookId = obs.bookId || '';"
            "      uid = obs.chapterUid || null;"
            "      if (obs.total) total = obs.total;"
            "      if (obs.idx !== undefined) idx = obs.idx;"
            "      if (idx < 0 && obs.localIdx !== undefined) idx = obs.localIdx;"
            "    }"
            "    const urlMatch = (location.href || '').match(/reader\\/([a-zA-Z0-9]+)/);"
            "    if (!bookId) bookId = urlMatch ? urlMatch[1] : '';"
            "    let hasLocal=false;"
            "    let localIdx=-1;"
            "    try {"
            "      const lcRaw = localStorage.getItem('book:lastChapters');"
            "      if (lcRaw) {"
            "        hasLocal=true;"
            "        const parsed = JSON.parse(lcRaw);"
            "        if (bookId && parsed && parsed[bookId]!=null) {"
            "          localIdx = parseInt(parsed[bookId]);"
            "          if (idx < 0) idx = localIdx;"
            "        }"
            "      }"
            "    } catch(e) {}"
            "    const near = (total > 0 && idx >= 0) ? (idx >= (total - 3)) : false;"
            "    if (near) {"
            "      fetch('https://weread.qq.com/web/shelf?nocache=' + Date.now(), {mode:'no-cors'}).catch(()=>{});"
            "    }"
            "    const ok = (total > 0 && idx >= 0);"
            "    return {ok, source, bookId, chapterUid: uid, idx, total, near, hasLocal, localIdx};"
            "  } catch(e) { return {ok:false, error:''+e}; }"
            "})();"
        );
        m_view->page()->runJavaScript(js, [this](const QVariant &res){
            const QVariantMap map = res.toMap();
            const bool ok = map.value(QStringLiteral("ok")).toBool();
            if (!ok && map.value(QStringLiteral("reason")).toString() == QLatin1String("no_state")) {
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (now - m_lastKeepaliveNoStateLogMs > 10000) {
                    qInfo() << "[KEEPALIVE]" << res;
                    m_lastKeepaliveNoStateLogMs = now;
                }
                return;
            }
            qInfo() << "[KEEPALIVE]" << res;
        });
    }
    void runDomDiagnostic() {
        if (!m_view || !m_view->page()) return;
        const QString js = QStringLiteral(
            "(() => {"
            "  const report={url:location.href};"
            "  const catSelectors=['.readerControls .catalog','.readerControls_item.catalog','.catalog','.readerCatalog','button[title*=\"\\u76ee\\u5f55\"]','[data-action=\"catalog\"]'];"
            "  const catFound=catSelectors.map(sel=>{const el=document.querySelector(sel);return el?{sel,visible:getComputedStyle(el).display!=='none',cls:el.className,tag:el.tagName}:null;}).filter(Boolean);"
            "  const catText=[];document.querySelectorAll('button,div[role=\"button\"],a,span').forEach(el=>{const t=(el.textContent||'').trim();if(t.includes('\\u76ee\\u5f55'))catText.push({tag:el.tagName,cls:el.className,visible:getComputedStyle(el).display!=='none'});});"
            "  const panel=document.querySelector('.readerCatalog');"
            "  report.catalog={found:catFound, byText:catText.slice(0,5), panelVisible: !!panel && getComputedStyle(panel).display!=='none'};"
            "  const dots=Array.from(document.querySelectorAll('.reader_font_control_slider_track_level_dot_container .reader_font_control_slider_track_level_dot'));"
            "  report.font={total:dots.length, dots:dots.slice(0,7).map(d=>({cls:d.className,active:/(show|active|current)/.test(d.className)})), panelVisible: !!document.querySelector('.readerControls') && getComputedStyle(document.querySelector('.readerControls')).display!=='none', bodyClass:document.body.className};"
            "  const themeBtns=Array.from(document.querySelectorAll('.readerControls .dark, .readerControls .white, .readerControls_item.dark, .readerControls_item.white'));"
            "  report.theme={count:themeBtns.length, buttons:themeBtns.slice(0,4).map(b=>({cls:b.className,vis:getComputedStyle(b).display!=='none'})), dataTheme: document.body.getAttribute('data-theme')||document.documentElement.getAttribute('data-theme')||'', bodyClass:document.body.className, htmlClass:document.documentElement.className};"
            "  const controls=Array.from(document.querySelectorAll('.readerControls > *')).slice(0,10).map(el=>({tag:el.tagName,cls:el.className,text:(el.textContent||'').trim().slice(0,30)}));"
            "  report.controls=controls;"
            "  return report;"
            "})();"
        );
        m_view->page()->runJavaScript(js, [](const QVariant &res){
            const auto map = res.toMap();
            qInfo() << "[DIAG] url" << map.value(QStringLiteral("url")).toString();
            const auto catalog = map.value(QStringLiteral("catalog")).toMap();
            qInfo() << "[DIAG] catalog found" << catalog.value(QStringLiteral("found")).toList().size()
                    << "byText" << catalog.value(QStringLiteral("byText")).toList().size()
                    << "panelVisible" << catalog.value(QStringLiteral("panelVisible")).toBool();
            const auto font = map.value(QStringLiteral("font")).toMap();
            qInfo() << "[DIAG] font dots" << font.value(QStringLiteral("total")).toInt()
                    << "panelVisible" << font.value(QStringLiteral("panelVisible")).toBool()
                    << "bodyClass" << font.value(QStringLiteral("bodyClass")).toString();
            const auto theme = map.value(QStringLiteral("theme")).toMap();
            qInfo() << "[DIAG] theme buttons" << theme.value(QStringLiteral("count")).toInt()
                    << "dataTheme" << theme.value(QStringLiteral("dataTheme")).toString()
                    << "bodyClass" << theme.value(QStringLiteral("bodyClass")).toString()
                    << "htmlClass" << theme.value(QStringLiteral("htmlClass")).toString();
            const auto controls = map.value(QStringLiteral("controls")).toList();
            QStringList ctrlSumm;
            for (const auto &c : controls) {
                const auto m = c.toMap();
                ctrlSumm << (m.value(QStringLiteral("tag")).toString() + ":" + m.value(QStringLiteral("cls")).toString());
            }
            qInfo() << "[DIAG] readerControls children" << ctrlSumm.join(" | ");
        });
    }
    void applyBookBoldIfNeeded() {
        if (!m_view || !m_view->page()) return;
        if (!isWeReadBook()) return; // 仅对微信书籍页加粗，避免干扰得到
        const QString js = QStringLiteral(
            "(() => {"
            "  if (!window.__WR_APPLY_BOLD) {"
            "    window.__WR_APPLY_BOLD = function(){"
            "      const ready = document.readyState === 'complete' || document.readyState === 'interactive';"
            "      if (!ready) { setTimeout(()=>window.__WR_APPLY_BOLD && window.__WR_APPLY_BOLD(), 100); return {ok:false, reason:'not-ready'}; }"
            "      const root = document.documentElement || document.body;"
            "      if (!root) { setTimeout(()=>window.__WR_APPLY_BOLD && window.__WR_APPLY_BOLD(), 100); return {ok:false, reason:'no-root'}; }"
            "      if (window.__WR_BOLD__) return {ok:true, skipped:true};"
            "      const style=document.createElement('style');"
            "      style.id='wr-bold-text';"
            "      style.textContent = '.renderTargetContent, .renderTargetContent * { font-weight: 600 !important; }';"
            "      try { root.appendChild(style); window.__WR_BOLD__=true; return {ok:true, applied:true}; }"
            "      catch(e){ setTimeout(()=>window.__WR_APPLY_BOLD && window.__WR_APPLY_BOLD(), 200); return {ok:false, reason:'append-fail', error:''+e}; }"
            "    };"
            "  }"
            "  return window.__WR_APPLY_BOLD();"
            "})();"
        );
        m_view->page()->runJavaScript(js, [this](const QVariant &res){
            qInfo() << "[STYLE] bold apply" << res;
            const QVariantMap m = res.toMap();
            if (m.value(QStringLiteral("ok")).toBool()) m_stylesOk = true;
        });
    }
    void applyPureTheme() {
        if (!m_view || !m_view->page()) return;
        const bool isBook = isWeReadBook();
        const QString js = QStringLiteral(
            "(() => {"
            "  if (!window.__WR_APPLY_THEME) {"
            "    window.__WR_APPLY_THEME = function(){"
            "      const ready = document.readyState === 'complete' || document.readyState === 'interactive';"
            "      if (!ready) { setTimeout(()=>window.__WR_APPLY_THEME && window.__WR_APPLY_THEME(), 100); return {ok:false, reason:'not-ready'}; }"
            "      const isBook = location.href.includes('/web/reader/');"
            "      const root = document.documentElement || document.body;"
            "      if (!root) { setTimeout(()=>window.__WR_APPLY_THEME && window.__WR_APPLY_THEME(), 100); return {ok:false, reason:'no-root'}; }"
            "      let style = document.getElementById('wr-pure-theme');"
            "      if (!isBook) { if (style) style.remove(); return {ok:true, removed:true}; }"
            "      if (!style) {"
            "        style = document.createElement('style'); style.id='wr-pure-theme';"
            "        try { root.appendChild(style); } catch(e) { setTimeout(()=>window.__WR_APPLY_THEME && window.__WR_APPLY_THEME(), 200); return {ok:false, reason:'append-fail', error: ''+e}; }"
            "      }"
            "      const cls = document.body.className || '';"
            "      const light = cls.includes('wr_whiteTheme');"
            "      const bg = light ? '#eeeeee' : '#111111';"
            "      const fg = light ? '#111111' : '#eeeeee';"
            "      style.textContent = `\n"
            "        .renderTargetContent, .renderTargetContent * { color:${fg} !important; background-color:${bg} !important; text-shadow: none !important; filter: none !important; }\n"
            "        .renderTargetContent img, .renderTargetContent svg, .renderTargetContent canvas, .renderTargetContent video { background-color: transparent !important; color: inherit !important; filter: none !important; }\n"
            "        * { transition: none !important; animation: none !important; text-shadow: none !important; filter: none !important; }\n"
            "      `;"
            "      return {ok:true, applied:true, light};"
            "    };"
            "  }"
            "  try { return window.__WR_APPLY_THEME(); } catch(e){ setTimeout(()=>window.__WR_APPLY_THEME && window.__WR_APPLY_THEME(), 200); return {ok:false, reason:'apply-error', error:''+e}; }"
            "})();"
        );
        m_view->page()->runJavaScript(js, [this](const QVariant &res){
            qInfo() << "[STYLE] pure-theme" << res;
            const QVariantMap m = res.toMap();
            if (m.value(QStringLiteral("ok")).toBool()) m_stylesOk = true;
        });
    }
    void hideCommonBanners() {
        if (!m_view || !m_view->page()) return;
        const QString js = QStringLiteral(
            "(() => {"
            "  try {"
            "    const hideTexts=['立即打开','去下载','打开App','打开 APP','下载App','App内打开','打开微信读书'];"
            "    const forceSel=["
            "      '.wr_index_page_header_download_action',"
            "      '.wr_index_page_header_download_wrapper',"
            "      '.wr_index_page_header_wrapper',"
            "      '.iget-invoke-app-bar',"
            "      '.invoke-app-btn',"
            "      '.iget-app-download',"
            "      '.download-app',"
            "      '.open-app',"
            "      '.openApp',"
            "      '.app-open',"
            "      '.wr_app_download',"
            "      '.wr_download',"
            "      '.wr_download_btn',"
            "      '.download-bar',"
            "      '.open-app-bar',"
            "      '.app-download-bar'"
            "    ];"
            "    // 精准命中微信首页下载条和得到首页的打开条\n"
            "    ['.wr_index_page_header_download_wrapper','.wr_index_page_header_download_action','.iget-invoke-app-bar','.invoke-app-btn'].forEach(sel=>{\n"
            "      document.querySelectorAll(sel).forEach(el=>{ if (el) el.style.display='none'; });\n"
            "    });"
            "    const nodes = Array.from(document.querySelectorAll('button,a,div,section'));"
            "    nodes.forEach(el => {"
            "      const t=(el.innerText||'').trim();"
            "      if(!t) return;"
            "      if(hideTexts.some(h=>t.includes(h))){"
            "        let bar = el.closest('.wr_index_page_header_download_action, .wr_index_page_header_download_wrapper, .wr_index_page_header_wrapper, .iget-invoke-app-bar, .iget-app-download, .app-open, .open-app, .openApp, .download-app, .wr_app_download, .wr_download, .wr_download_btn, .download-bar, .open-app-bar, .app-download-bar');"
            "        if (!bar) bar = el;"
            "        if (bar === document.body || bar === document.documentElement) return;"
            "        const h = bar.getBoundingClientRect ? bar.getBoundingClientRect().height : 0;"
            "        if (h===0 || h>400) return;"
            "        bar.style.display='none';"
            "      }"
            "    });"
            "    return {ok:true, hidden:true};"
            "  }catch(e){ return {ok:false, error:String(e)}; }"
            "})();"
        );
        m_view->page()->runJavaScript(js, [this](const QVariant &res){
            qInfo() << "[STYLE] banner hide" << res;
            m_bannerHidden = true;
        });
    }
    void applyBookEnhancements() {
        // 微信读书书籍页：仅施加容器占位，避免 0 高度；不改字体/分页
        if (isWeReadBook()) {
            if (!m_view || !m_view->page()) return;
            const QString js = QStringLiteral(
                "(() => {"
                "  try {"
                "    const styleId='wr-force-container';"
                "    let st=document.getElementById(styleId);"
                "    if (!st) { st=document.createElement('style'); st.id=styleId; (document.documentElement||document.body).appendChild(st); }"
                "    st.textContent = `"
                "      .renderTargetContent, iframe { min-height:100vh !important; width:100% !important; opacity:1 !important; visibility:visible !important; position:relative !important; display:block !important; }"
                "      iframe { min-height:100vh !important; width:100% !important; border:0 !important; }"
                "    `;"
            "    // 强制白色主题，确保白度判断有效，同时覆盖 iframe 内部"
            "    const applyWhite = (doc) => {"
            "      if (!doc) return;"
                "      const root = doc.documentElement || doc.body;"
                "      if (!root) return;"
                "      try {"
                "        doc.body && doc.body.classList.add('wr_whiteTheme');"
                "        doc.body && doc.body.classList.remove('wr_darkTheme');"
                "        doc.body && doc.body.setAttribute('data-theme','light');"
                "        doc.documentElement && doc.documentElement.setAttribute('data-theme','light');"
                "      } catch(_e) {}"
                "      try {"
                "        doc.cookie = 'wr_theme=wr_whiteTheme; domain=.weread.qq.com; path=/; max-age=31536000';"
                "        if (doc.defaultView && doc.defaultView.localStorage) {"
                "          doc.defaultView.localStorage.setItem('wr_theme','wr_whiteTheme');"
                "          doc.defaultView.localStorage.setItem('wrTheme','wr_whiteTheme');"
                "        }"
                "      } catch(_e) {}"
                "      let themeStyle = doc.getElementById('wr-pure-theme');"
                "      if (!themeStyle) { themeStyle = doc.createElement('style'); themeStyle.id='wr-pure-theme'; (doc.documentElement||doc.body).appendChild(themeStyle); }"
                "      if (themeStyle) {"
                "        themeStyle.textContent = `"
                "          body, html, .renderTargetContent, .renderTargetContent * { background:#ffffff !important; color:#111111 !important; text-shadow:none !important; filter:none !important; }"
                "          img, canvas, video, svg { background-color:transparent !important; filter:none !important; }"
                "        `;"
                "      }"
                "    };"
            "    applyWhite(document);"
            "    // 如果正文在 iframe 内，尝试同步主题"
            "    Array.from(document.querySelectorAll('iframe')).forEach(f => {"
            "      try {"
            "        const d = f.contentDocument || (f.contentWindow && f.contentWindow.document);"
            "        applyWhite(d);"
            "        if (d) {"
            "          const logReady = () => {"
            "            try {"
            "              const b = d.body || d.documentElement;"
            "              const r = b ? b.getBoundingClientRect() : {width:0,height:0,x:0,y:0};"
            "              const len = b && b.innerText ? b.innerText.length : 0;"
            "              console.warn('[IFRAME_READY]', JSON.stringify({src:f.src||'', rect:{x:r.x,y:r.y,w:r.width,h:r.height}, textLen: len}));"
            "            } catch(_) {}"
            "          };"
            "          d.addEventListener('DOMContentLoaded', logReady, {once:true});"
            "          d.addEventListener('load', logReady, {once:true});"
            "          d.defaultView && d.defaultView.requestAnimationFrame(() => { setTimeout(logReady, 0); });"
            "        }"
            "      } catch(_e) {}"
            "    });"
            "    return {ok:true, bodyCls:(document.body && document.body.className)||'', dataTheme:(document.body && document.body.getAttribute('data-theme'))||'', cookie:document.cookie};"
            "  } catch(e) { return {ok:false, error:String(e)}; }"
            "})()"
            );
            m_view->page()->runJavaScript(js, [](const QVariant &res){
                qInfo() << "[STYLE] wr-force-container" << res;
            });

            // 添加点击翻页功能：左侧点击上一页，右侧点击下一页
            const QString clickPagerJs = QStringLiteral(
                "(() => {"
                "  try {"
                "    const container = document.querySelector('.renderTargetContent') || document.body;"
                "    const fireClick = (btn) => {"
                "      if (!btn) return false;"
                "      const span = btn.querySelector('span') || btn;"
                "      // 模拟用户点击：pointerdown → pointerup → click"
                "      const evts = ['pointerdown','pointerup','click'];"
                "      evts.forEach(t => span.dispatchEvent(new PointerEvent(t, {bubbles:true, cancelable:true})));"
                "      return true;"
                "    };"
                "    const clickHandler = (e) => {"
                "      if (!container) return;"
                "      const rect = container.getBoundingClientRect();"
                "      const clickX = e.clientX - rect.left;"
                "      const midX = rect.width / 2;"
                "      const isLeftSide = clickX < midX;"
                "      console.log('[CLICK_PAGER]', 'x=' + Math.round(clickX) + ' width=' + Math.round(rect.width) + ' side=' + (isLeftSide ? 'left:PREV' : 'right:NEXT'));"
                "      if (isLeftSide) {"
                "        const prevBtn = document.querySelector('.renderTarget_pager_button');"
                "        fireClick(prevBtn);"
                "      } else {"
                "        const nextBtn = document.querySelector('.renderTarget_pager_button_right');"
                "        fireClick(nextBtn);"
                "      }"
                "    };"
                "    document.removeEventListener('click', window.__wereadClickPager, true);"
                "    window.__wereadClickPager = clickHandler;"
                "    document.addEventListener('click', clickHandler, true);"
                "    console.log('[CLICK_PAGER] initialized');"
                "    return {ok:true};"
                "  } catch(e) { return {ok:false, error:String(e)}; }"
                "})()"
            );
            m_view->page()->runJavaScript(clickPagerJs, [](const QVariant &res){
                qInfo() << "[CLICK_PAGER] setup result" << res;
            });
            return;
        }
        applyBookBoldIfNeeded();
        applyPureTheme();
        applyDedaoNarrow();
        installChapterObserver();
        hideCommonBanners();
    }
    void runDedaoProbe() {
        if (!m_view || !m_view->page()) return;
        const QString js = R"(
            (() => {
                try {
                    const report = { url: location.href, settingsOpened:false, themeButtons:[], fontPanel:{}, scrollables:[] };

                    const settingBtn = Array.from(document.querySelectorAll('button, [role=button]'))
                        .find(b => (b.innerText||'').trim().includes('设置') || b.classList.contains('reader-tool-button'));
                    report.hasSettingBtn = !!settingBtn;

                    const themeBtns = Array.from(document.querySelectorAll('.tool-box-theme-group .reader-tool-theme-button'));
                    report.themeButtons = themeBtns.slice(0,6).map((btn,i)=>({
                        idx:i, text:(btn.innerText||'').trim(), cls:btn.className,
                        selected:btn.classList.contains('reader-tool-theme-button-selected'),
                        visible:getComputedStyle(btn).display!=='none'
                    }));

                    const fontBox = document.querySelector('.set-tool-box-change-font');
                    const fontBtns = fontBox ? Array.from(fontBox.querySelectorAll('button')) : [];
                    report.fontPanel = { boxFound: !!fontBox, btnCount: fontBtns.length };

                    const all = document.querySelectorAll('*');
                    all.forEach(el=>{
                        if (el.scrollHeight > el.clientHeight + 20) {
                            report.scrollables.push({tag:el.tagName, cls:el.className, id:el.id, viewH:el.clientHeight, scrollH:el.scrollHeight});
                        }
                    });
                    report.scrollables = report.scrollables.slice(0,10);
                    return report;
                } catch(e) { return {error:String(e)}; }
            })()
        )";
        m_view->page()->runJavaScript(js, [](const QVariant &res){
            qInfo() << "[DEDAO_PROBE]" << res;
        });
    }
    void applyDedaoNarrow() {
        if (!m_view || !m_view->page()) return;
        if (!isDedaoBook()) return;
        const QString js = QStringLiteral(
            "(() => {"
            "  try {"
            "    let style = document.getElementById('dedao-compact');"
            "    if (!style) { style = document.createElement('style'); style.id='dedao-compact'; }"
            "    style.textContent = "
            "      'body, .reader-root, .reader, .reader-content, .iget-reader-container {'"
            "      + 'margin-left:0!important;margin-right:0!important;padding-left:0!important;padding-right:0!important;max-width:100%!important;'"
            "      + '}'"
            "      + '.reader-content, .reader, .reader-root, .iget-reader-container {width:100%!important;}';"
            "    const root = document.documentElement || document.body;"
            "    if (root && style.parentNode !== root) root.appendChild(style);"
            "  } catch(e) { return {ok:false, reason:'style', error:String(e)}; }"
            "  try {"
            "    const hideTexts=['立即打开','去下载'];"
            "    const candidates = Array.from(document.querySelectorAll('button, a, div'));"
            "    candidates.forEach(el => {"
            "      const t = (el.innerText || '').trim();"
            "      if (!t) return;"
            "      if (hideTexts.some(h => t.includes(h))) {"
            "        const bar = el.closest('.iget-app-download, .app-open, .open-app, .openApp, .download-app') || el;"
            "        bar.style.display = 'none';"
            "      }"
            "    });"
            "  } catch(e) { return {ok:false, reason:'banner', error:String(e)}; }"
            "  return {ok:true};"
            "})();"
        );
        m_view->page()->runJavaScript(js, [](const QVariant &res){
            qInfo() << "[STYLE] dedao compact" << res;
        });
    }
    void installChapterObserver() {
        if (!m_view || !m_view->page()) return;
        const bool isBook = currentUrl.toString().contains(QStringLiteral("/web/reader/"));
        if (!isBook) return;
        const QString js = QStringLiteral(
            "(() => {"
            "  if (window.__WR_OBSERVER_INSTALLED__) return {ok:true, skipped:true};"
            "  window.__WR_OBSERVER_INSTALLED__ = true;"
            "  window.__WR_CHAPTER_OBS = window.__WR_CHAPTER_OBS || {};"
            "  window.__WR_LOADFAIL = window.__WR_LOADFAIL || {active:false,retries:0,last:0};"
            "  function log(msg){ try{ console.log(msg);}catch(e){} }"
            "  function updateLocalIdx(){"
            "    try{"
            "      const m=(location.href||'').match(/reader\\/([a-zA-Z0-9]+)/);"
            "      const bid=m?m[1]:'';"
            "      const raw=localStorage.getItem('book:lastChapters');"
            "      if(raw){"
            "        const parsed=JSON.parse(raw);"
            "        if(bid && parsed && parsed[bid]!=null){"
            "          window.__WR_CHAPTER_OBS.localIdx=parseInt(parsed[bid]);"
            "          window.__WR_CHAPTER_OBS.bookId=window.__WR_CHAPTER_OBS.bookId||bid;"
            "        }"
            "      }"
            "    }catch(e){}"
            "  }"
            "  updateLocalIdx();"
            "  function checkLoadFail(){"
            "    try {"
            "      const errNode = document.querySelector('.chapterContentError, .readerError, .renderTargetContent .wr_error');"
            "      let retryBtn = null;"
            "      if (errNode){"
            "        const btns = Array.from(document.querySelectorAll('button, div[role=\"button\"], a'));"
            "        retryBtn = btns.find(b => { const t = (b.innerText||'').trim(); return t.includes('重试') || t.includes('加载失败'); });"
            "      }"
            "      if (retryBtn){"
            "        retryBtn.style.display='none';"
            "        if (!window.__WR_LOADFAIL.active){"
            "          window.__WR_LOADFAIL.active = true;"
            "          window.__WR_LOADFAIL.retries = 0;"
            "          window.__WR_LOADFAIL.start = Date.now();"
            "          log('[LOADFAIL] found');"
            "        }"
            "      } else {"
            "        if (window.__WR_LOADFAIL.active) log('[LOADFAIL] cleared');"
            "        window.__WR_LOADFAIL.active = false;"
            "      }"
            "    } catch(e) { log('[LOADFAIL] err ' + e); }"
            "  }"
            "  function doRetry(){"
            "    try {"
            "      if (!window.__WR_LOADFAIL.active) return;"
            "      const start = window.__WR_LOADFAIL.start || Date.now();"
            "      const elapsed = Date.now() - start;"
            "      if (elapsed > 120000){ log('[LOADFAIL] timeout after 2m'); window.__WR_LOADFAIL.active=false; return; }"
            "      const btn = Array.from(document.querySelectorAll('button, div[role=\"button\"], a')).find(b=> { const t=(b.innerText||'').trim(); return t.includes('重试'); });"
            "      window.__WR_LOADFAIL.retries = (window.__WR_LOADFAIL.retries||0)+1;"
            "      if (btn && btn.click){ btn.click(); log('[LOADFAIL] retry #' + window.__WR_LOADFAIL.retries + ' via click'); }"
            "      else { location.reload(); log('[LOADFAIL] retry #' + window.__WR_LOADFAIL.retries + ' via reload'); }"
            "    } catch(e) { log('[LOADFAIL] retry err ' + e); }"
            "  }"
            "  setInterval(checkLoadFail, 5000);"
            "  setInterval(doRetry, 15000);"
            "  return {ok:true, installed:true, obs: window.__WR_CHAPTER_OBS};"
            "})();"
        );
        m_view->page()->runJavaScript(js, [](const QVariant &res){
            qInfo() << "[OBS] install" << res;
        });
    }
public:
    void cycleUserAgentMode() {
        static const QStringList modes = { QStringLiteral("default"), QStringLiteral("android"), QStringLiteral("kindle") };
        static int idx = 0;
        idx = (idx + 1) % modes.size();
        const QString mode = modes.at(idx);
        // 依据模式选择 UA，并同时应用到微信/非微信页面，随后强制重载
        const QString uaDefault = m_profile ? m_profile->httpUserAgent() : QString();
        const QString kindleUA = QStringLiteral("Mozilla/5.0 (Linux; Kindle Paperwhite) AppleWebKit/537.36 (KHTML, like Gecko) Silk/3.2 Mobile Safari/537.36");
        const QString androidUA = QStringLiteral("Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/129.0.0.0 Mobile Safari/537.36");
        const QString iosSafariUA = QStringLiteral("Mozilla/5.0 (iPhone; CPU iPhone OS 17_1 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1");

        QString targetUA = iosSafariUA; // default
        if (mode.compare(QStringLiteral("kindle"), Qt::CaseInsensitive) == 0) {
            targetUA = kindleUA;
        } else if (mode.compare(QStringLiteral("android"), Qt::CaseInsensitive) == 0) {
            targetUA = androidUA;
        } else if (mode.compare(QStringLiteral("default"), Qt::CaseInsensitive) == 0 && !uaDefault.isEmpty()) {
            targetUA = uaDefault;
        }

        // 记录环境变量，便于外部脚本感知
        qputenv("WEREAD_UA_MODE", mode.toUtf8());

        // 同步更新 UA 配置：微信书籍与非书籍统一使用当前模式的 UA
        m_uaNonWeRead = targetUA;
        m_uaWeReadBook = targetUA;
        m_currentUA = QStringLiteral("unset"); // 触发后续 update 生效

        // 停止当前轮询/抓帧，准备重载
        stopReadyWatch();
        cancelPendingCaptures();
        m_firstFrameDone = false;
        m_reloadAttempts = 0;

        // 应用新的 UA 并强制重载当前 URL
        updateUserAgentForUrl(currentUrl);
        // 以实际生效的 UA 回填模式并更新按钮，避免误导
        m_uaMode = detectUaMode(m_profile ? m_profile->httpUserAgent() : targetUA);
        updateUaButtonText(m_uaMode);
        m_lastReloadTs = QDateTime::currentMSecsSinceEpoch();
        qInfo() << "[UA] cycle to" << m_uaMode << "UA:" << targetUA << "reload" << currentUrl << "ts" << m_lastReloadTs;
        if (m_view) {
            m_view->load(currentUrl);
        }
    }

    void updateUaButtonText(const QString &mode) {
        if (!m_uaButton) return;
        auto norm = [](const QString &m) -> QString {
            if (m.compare(QStringLiteral("android"), Qt::CaseInsensitive) == 0 ||
                m.compare(QStringLiteral("mobile"), Qt::CaseInsensitive) == 0) {
                return QStringLiteral("Mobile");
            }
            if (m.compare(QStringLiteral("kindle"), Qt::CaseInsensitive) == 0) {
                return QStringLiteral("Kindle");
            }
            if (m.compare(QStringLiteral("web"), Qt::CaseInsensitive) == 0 ||
                m.compare(QStringLiteral("desktop"), Qt::CaseInsensitive) == 0 ||
                m.compare(QStringLiteral("default"), Qt::CaseInsensitive) == 0) {
                return QStringLiteral("Web");
            }
            return m.isEmpty() ? QStringLiteral("?") : m;
        };

        const QString uaLabel = norm(mode);
        const QString bookLabel = norm(m_weReadBookMode);
        QString label = QStringLiteral("UA:%1").arg(uaLabel);
        // 对于微信书籍，优先显示当前书籍模式；若与 UA 不同则同时提示
        if (isWeReadBook()) {
            if (!bookLabel.isEmpty() && bookLabel.compare(uaLabel, Qt::CaseInsensitive) != 0) {
                label = QStringLiteral("UA:%1|Book:%2").arg(uaLabel, bookLabel);
            } else if (!bookLabel.isEmpty()) {
                label = QStringLiteral("UA:%1").arg(bookLabel);
            }
        }
        m_uaButton->setText(label);
    }

    void logUrlState(const QString &tag = QString()) {
        const QString u = currentUrl.toString();
        qInfo() << "[URL]" << tag << u
                << "isWeReadBook" << isWeReadBook()
                << "isDedao" << isDedaoBook();
    }

    // 简易探针：记录当前 URL、滚动容器和主要正文节点
    void logPageProbe(const QString &tag = QString()) {
        if (!m_view || !m_view->page()) return;
        const QString js = QStringLiteral(
            "(() => {"
            "  const url = location.href || '';"
            "  const host = location.host || '';"
            "  const path = location.pathname || '';"
            "  const cont = document.querySelector('.renderTargetContent');"
            "  const docEl = document.documentElement;"
            "  const scrollEl = document.scrollingElement || docEl || document.body;"
            "  const info = {"
            "    url, host, path,"
            "    hasRenderTarget: !!cont,"
            "    scrollElTag: scrollEl ? scrollEl.tagName : '',"
            "    scrollH: scrollEl ? scrollEl.scrollHeight : 0,"
            "    clientH: scrollEl ? scrollEl.clientHeight : 0,"
            "    offsetH: scrollEl ? scrollEl.offsetHeight : 0,"
            "  };"
            "  return info;"
            "})()"
        );
        m_view->page()->runJavaScript(js, [tag](const QVariant &v){
            qInfo() << "[PROBE]" << tag << v;
        });
    }

    // 记录当前页面脚本清单，辅助检查脚本截断/重复加载
    void logScriptInventory(bool once = true) {
        if (once && m_scriptInventoryLogged) return;
        if (!m_view || !m_view->page()) return;
        if (!isWeReadBook() && !isDedaoBook()) return;
        if (once) m_scriptInventoryLogged = true;
        const QString js = QStringLiteral(
            "(() => {"
            "  try {"
            "    const list = Array.from(document.scripts || []);"
            "    const mapped = list.map((s, idx) => ({idx, src: s.src || '', inlineLen: (s.innerText || '').length}));"
            "    console.warn('[SCRIPT_LIST]', JSON.stringify(mapped));"
            "    return mapped;"
            "  } catch(e) { return {error:String(e)}; }"
            "})()"
        );
        m_view->page()->runJavaScript(js, [](const QVariant &res){
            qInfo() << "[SCRIPT_LIST]" << res;
        });
    }

    // 记录性能资源条目，关注 app/js/css 是否加载、大小
    void logResourceEntries(bool once = true) {
        if (once && m_resourceLogged) return;
        if (!m_view || !m_view->page()) return;
        if (!isWeReadBook() && !isDedaoBook()) return;
        if (once) m_resourceLogged = true;
        const QString js = QStringLiteral(
            "(() => {"
            "  try {"
            "    const entries = performance.getEntriesByType('resource') || [];"
            "    const filtered = entries.filter(e => /app\\.|wrweb|wrwebnjlogic/i.test(e.name || ''));"
            "    const mapped = filtered.map(e => ({"
            "      name: e.name, type: e.initiatorType, transferSize: e.transferSize, encodedSize: e.encodedBodySize, decodedSize: e.decodedBodySize, dur: e.duration, respEnd: e.responseEnd"
            "    }));"
            "    console.warn('[RES_ENTRY]', JSON.stringify(mapped));"
            "    return mapped;"
            "  } catch(e) { return {error:String(e)}; }"
            "})()"
        );
        m_view->page()->runJavaScript(js, [](const QVariant &res){
            qInfo() << "[RES_ENTRY]" << res;
        });
    }

    // 全量扫描文本长度/画布/图片数量，用于判断非 DOM 文本渲染
    void logTextScan(bool once = true) {
        if (once && m_textScanDone) return;
        if (!m_view || !m_view->page()) return;
        if (!isWeReadBook() && !isDedaoBook()) return;
        if (once) m_textScanDone = true;
        const QString js = QStringLiteral(
            "(() => {"
            "  try {"
            "    const bodyLen = (document.body && document.body.innerText) ? document.body.innerText.length : 0;"
            "    let shadowText = 0;"
            "    const all = Array.from(document.querySelectorAll('*'));"
            "    let canvasCount = 0, imgCount = 0;"
            "    all.forEach(el => {"
            "      const tag = (el.tagName || '').toLowerCase();"
            "      if (tag === 'canvas') canvasCount++;"
            "      if (tag === 'img') imgCount++;"
            "      if (el.shadowRoot && el.shadowRoot.innerText) shadowText += el.shadowRoot.innerText.length;"
            "    });"
            "    let iframeText = 0; let iframeBlocked = 0; let iframeCount = 0;"
            "    Array.from(document.querySelectorAll('iframe')).forEach(f => {"
            "      iframeCount++;"
            "      try {"
            "        const d = f.contentDocument || (f.contentWindow && f.contentWindow.document);"
            "        if (d && d.body && d.body.innerText) iframeText += d.body.innerText.length;"
            "      } catch(_) { iframeBlocked++; }"
            "    });"
            "    // 也统计 renderTarget/readerChapter 节点长度，方便对比"
            "    const core = document.querySelector('.renderTargetContent') || document.querySelector('.readerChapterContent');"
            "    const coreLen = core ? ((core.innerText||'').length) : 0;"
            "    return {bodyLen, shadowText, iframeText, iframeCount, iframeBlocked, canvasCount, imgCount};"
          "  } catch(e) { return {error:String(e)}; }"
            "})()"
        );
        m_view->page()->runJavaScript(js, [](const QVariant &res){
            qWarning().noquote() << "[TEXT_SCAN]" << res;
        });
    }

    // 微信读书：滚动替代分页，步长为屏高 0.8，滚动后对齐行高
    void weReadScroll(bool forward) {
        if (!m_view || !m_view->page()) { m_navigating = false; return; }
        const int currentSeq = ++m_navSequence;
        m_navigating = true;
        const int stepPx = qRound((m_view ? m_view->height() : 1600) * (forward ? 0.8 : -0.8));
        const qint64 cppSend = QDateTime::currentMSecsSinceEpoch();
        qInfo() << "[PAGER] weRead scroll dispatch" << "forward" << forward << "step" << stepPx << "seq" << currentSeq << "ts" << cppSend;

        QTimer::singleShot(1200, this, [this, currentSeq](){
            if (m_navigating && m_navSequence == currentSeq) {
                m_navigating = false;
                qWarning() << "[PAGER] weRead force unlock (timeout 1200ms) seq" << currentSeq;
                startReadyWatch();
            }
        });

        const QString js = QStringLiteral(
            R"(
            (() => {
                try {
                    const t_js_start = Date.now();
                    const step = %1;
                    const el = document.querySelector('.renderTargetContent') || document.scrollingElement || document.documentElement;
                    if (!el) return {ok:false, reason:'no-container'};
                    const before = el.scrollTop || 0;
                    el.scrollBy(0, step);
                    const after = el.scrollTop || 0;
                    // 对齐到行高整数倍，减少半截行
                    const sample = el.querySelector('p, div, span');
                    const lh = sample ? parseFloat(getComputedStyle(sample).lineHeight)||32 : 32;
                    const padTop = parseFloat(getComputedStyle(el).paddingTop)||0;
                    const aligned = Math.max(0, Math.round((after - padTop)/lh)*lh + padTop);
                    if (Math.abs(aligned - after) > 0.5) el.scrollTo({top: aligned, behavior:'auto'});
                    const t_js_end = Date.now();
                    return {ok:true, before, after:aligned, delta: aligned-before, t_js_start, t_js_end, tag: el.tagName};
                } catch(e) { return {ok:false, error:String(e)}; }
            })()
            )"
        ).arg(stepPx);

        m_view->page()->runJavaScript(js, [this, cppSend, currentSeq, forward](const QVariant &res){
            const qint64 cppRecv = QDateTime::currentMSecsSinceEpoch();
            const QVariantMap m = res.toMap();
            const qint64 jsStart = m.value(QStringLiteral("t_js_start")).toLongLong();
            const qint64 jsEnd   = m.value(QStringLiteral("t_js_end")).toLongLong();
            const qint64 total   = cppRecv - cppSend;
            const qint64 queue   = jsStart ? (jsStart - cppSend) : -1;
            const qint64 exec    = (jsEnd && jsStart) ? (jsEnd - jsStart) : -1;
            const qint64 ipc     = (jsEnd) ? (cppRecv - jsEnd) : -1;
            const QString tag    = m.value(QStringLiteral("tag")).toString();
            const int delta      = m.value(QStringLiteral("delta")).toInt();

            if (total > 800) {
                qWarning().noquote() << QString("[PAGER] weRead SLOW | Total:%1ms Queue:%2ms Exec:%3ms IPC:%4ms Tag:%5 Delta:%6")
                                        .arg(total).arg(queue).arg(exec).arg(ipc).arg(tag).arg(delta);
            } else {
                qInfo().noquote() << QString("[PAGER] weRead OK   | Total:%1ms Queue:%2ms Exec:%3ms Tag:%4 Delta:%5")
                                     .arg(total).arg(queue).arg(exec).arg(tag).arg(delta);
            }
            if (isWeReadBook() && delta == 0) {
                qWarning().noquote() << QString("[PAGER] weRead scroll delta=0, fallback to pager button (%1)")
                                        .arg(forward ? QStringLiteral("next") : QStringLiteral("prev"));
                const QString clickJs = forward
                    ? QStringLiteral(
                          "(() => {"
                          "  const btn = document.querySelector('.renderTarget_pager_button_right');"
                          "  if (!btn) return {ok:false, reason:'no-next-btn'};"
                          "  const span = btn.querySelector('span') || btn;"
                          "  ['pointerdown','pointerup','click'].forEach(t=>span.dispatchEvent(new PointerEvent(t,{bubbles:true,cancelable:true})));"
                          "  return {ok:true, action:'next'};"
                          "})()")
                    : QStringLiteral(
                          "(() => {"
                          "  const btn = document.querySelector('.renderTarget_pager_button');"
                          "  if (!btn) return {ok:false, reason:'no-prev-btn'};"
                          "  const span = btn.querySelector('span') || btn;"
                          "  ['pointerdown','pointerup','click'].forEach(t=>span.dispatchEvent(new PointerEvent(t,{bubbles:true,cancelable:true})));"
                          "  return {ok:true, action:'prev'};"
                          "})()");
                m_view->page()->runJavaScript(clickJs, [forward](const QVariant &r){
                    qInfo() << "[PAGER] weRead pager fallback result"
                            << (forward ? "next" : "prev") << r;
                });
            }
            QTimer::singleShot(300, this, [this](){ startReadyWatch(); });
            if (m_navSequence == currentSeq) {
                m_navigating = false;
            }
        });
    }
    void triggerStalledRescue(const QString &reason, int chapterLen, int bodyLen, int pollCount) {
        if (!m_view || !m_view->page() || !isWeReadBook()) return;
        m_stallRescueTriggered = true;
        const int seq = ++m_rescueSeq;
        const QString js = QStringLiteral(
            "(() => {"
            "  const btn = document.querySelector('.renderTarget_pager_button_right');"
            "  if (btn && btn.click) { btn.click(); return {ok:true, action:'click-next'}; }"
            "  location.reload();"
            "  return {ok:true, action:'reload'};"
            "})()"
        );
        m_view->page()->runJavaScript(js, [this, seq, reason, chapterLen, bodyLen, pollCount](const QVariant &res){
            qWarning() << "[RESCUE] stalled seq" << seq
                       << "reason" << reason
                       << "pollCount" << pollCount
                       << "chapterLen" << chapterLen
                       << "bodyLen" << bodyLen
                       << res;
            m_readyPollCount = 0;
            m_emptyReadyPolls = 0;
            startReadyWatch();
        });
    }
};

class TouchBridge : public QObject {
    Q_OBJECT
public:
    explicit TouchBridge(WereadBrowser* browser, QObject* parent = nullptr)
        : QObject(parent), m_browser(browser) {
        const QHostAddress bindAddr(QStringLiteral("127.0.0.1"));
        const bool ok = m_socket.bind(bindAddr, 45454,
                                      QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
        qInfo() << "[TOUCH] bind" << ok << "addr" << bindAddr.toString() << "port" << 45454;
        connect(&m_socket, &QUdpSocket::readyRead, this, &TouchBridge::onReady);
    }
private slots:
    void onReady() {
        while (m_socket.hasPendingDatagrams()) {
            QByteArray datagram;
            datagram.resize(int(m_socket.pendingDatagramSize()));
            m_socket.readDatagram(datagram.data(), datagram.size());
            if (datagram.size() < int(sizeof(Payload))) continue;
            Payload p;
            memcpy(&p, datagram.constData(), sizeof(Payload));
            QPointF pt(p.x, p.y);
            const qint64 tsMs = QDateTime::currentMSecsSinceEpoch();
            m_lastTouchMs = tsMs;
            qInfo() << "[TOUCH] recv type" << p.type << "pos" << pt << "ts" << tsMs;
            if (m_browser) m_browser->updateLastTouchTs(tsMs);
            if (p.type == 1 || p.type == 103 || p.type == 104) {
                m_browser->stopReadyWatch();
            }
            switch (p.type) {
            case 101: // EXIT
                m_browser->exitToXochitl();
                break;
            case 102: // BACK
                m_browser->allowDedaoDetailOnce();
                m_browser->goBack();
                m_browser->scheduleBookCaptures();
                break;
            case 103: // NEXT_PAGE
                qInfo() << "[FLOW] dispatch nextPage ts" << tsMs << "isDedao" << m_browser->isDedaoBook();
                m_browser->goNextPage();
                // 翻页改为固定 5 帧抓帧
                m_browser->schedulePageTurnCaptures();
                m_browser->restartReadyWatch();
                break;
            case 104: // PREV_PAGE
                qInfo() << "[FLOW] dispatch prevPage ts" << tsMs << "isDedao" << m_browser->isDedaoBook();
                m_browser->goPrevPage();
                m_browser->schedulePageTurnCaptures();
                m_browser->restartReadyWatch();
                break;
            case 105: // SCROLL_UP
                m_browser->injectWheel(pt, -120);
                m_browser->scrollByJs(-m_browser->pageStep());
                break;
            case 106: // SCROLL_DOWN
                m_browser->injectWheel(pt, 120);
                m_browser->scrollByJs(m_browser->pageStep());
                break;
            case 107: // MENU_OPEN
                m_browser->showMenu();
                m_browser->scheduleBookCaptures();
                break;
            case 108: // THEME_TOGGLE (临时改为主题切换，便于写 cookie/LS)
                m_browser->toggleTheme();
                m_browser->scheduleBookCaptures();
                break;
            case 112: // SERVICE_TOGGLE
                m_browser->toggleService();
                m_browser->scheduleBookCaptures();
                break;
            case 109: // MENU_FONT_UP
                m_browser->adjustFont(true);
                m_browser->scheduleBookCaptures();
                break;
            case 110: // MENU_FONT_DOWN
                m_browser->adjustFont(false);
                m_browser->scheduleBookCaptures();
                break;
            case 111: // MENU_THEME
                qInfo() << "[MENU] UA toggle (from viewer type 111)";
                m_browser->cycleUserAgentMode();
                m_browser->scheduleBookCaptures();
                break;
            case 1:
                m_lastPos = pt;
                m_pressed = true;
                m_browser->injectMouse(Qt::LeftButton, QEvent::MouseButtonPress, pt);
                m_pressTimer.restart();
                break;
            case 2:
                if (!m_pressed) {
                    m_browser->injectMouse(Qt::LeftButton, QEvent::MouseButtonPress, pt);
                    m_pressed = true;
                }
                if (m_lastPos.isNull()) m_lastPos = pt;
                m_accumDy += int(pt.y() - m_lastPos.y());
                m_lastPos = pt;
                m_browser->injectMouse(Qt::LeftButton, QEvent::MouseMove, pt);
                break;
            case 3: {
                if (!m_pressed) {
                    m_browser->injectMouse(Qt::LeftButton, QEvent::MouseButtonPress, pt);
                }
                m_browser->injectMouse(Qt::LeftButton, QEvent::MouseButtonRelease, pt);
                const qint64 pressMs = m_pressTimer.isValid() ? m_pressTimer.elapsed() : 0;
                const bool longPress = pressMs > 500;
                if (longPress || std::abs(m_accumDy) <= 5) {
                    if (m_browser->isDedaoBook()) {
                        m_browser->openDedaoMenu();
                        // 调试菜单面板状态
                        QWebEngineView* v = m_browser->getView();
                        if (v && v->page()) {
                            checkMenuPanelState(v->page());
                        }
                    } else {
                        m_browser->hittestJs(pt); // 长按也当作点击，便于呼出菜单
                    }
                } else {
                    m_browser->injectWheel(pt, -m_accumDy);      // wheel event
                    m_browser->scrollByJs(m_accumDy);            // JS fallback scroll
                }
                // 统一走 ready 抓帧（6 连拍），并重置计时
                m_browser->restartCaptureLoop();
                // 点击释放后重启 READY 轮询，便于快速响应新页面/刷新
                m_browser->restartReadyWatch();
                m_accumDy = 0;
                m_lastPos = QPointF();
                m_pressed = false;
                break;
            }
            default: break;
            }
        }
    }
private:
    struct Payload { quint8 type; quint8 pad[3]; float x; float y; };
    QUdpSocket m_socket;
    WereadBrowser* m_browser = nullptr;
    QPointF m_lastPos;
    int m_accumDy = 0;
    bool m_pressed = false;
    QElapsedTimer m_pressTimer;
    qint64 m_lastTouchMs = 0;
};

int main(int argc, char* argv[]) {
    g_prevHandler = qInstallMessageHandler(filteredMessageHandler);
    qSetMessagePattern(QStringLiteral("%{time yyyy-MM-dd hh:mm:ss.zzz} [%{type}] %{message}"));
    auto ensureEnv = [](const char* name, const QByteArray& value) {
        if (!qEnvironmentVariableIsSet(name) || qEnvironmentVariable(name).isEmpty())
            qputenv(name, value);
    };
    ensureEnv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    ensureEnv("QTWEBENGINE_DISABLE_SANDBOX", QByteArrayLiteral("1"));
    // 强制设置 Chromium flags，避免启动脚本未传递导致缺失
    // 方案E优化：启用HTTP缓存、HTTP/2、Brotli压缩，增加渲染线程数
    QByteArray chromiumFlags =
        QByteArrayLiteral("--disable-gpu --disable-gpu-compositing "
                          "--disable-webgl --disable-accelerated-video-decode "
                          "--disable-smooth-scrolling --num-raster-threads=6 "
                          "--no-sandbox --touch-events=enabled "
                          "--disk-cache-size=524288000 "  // 500MB (原50MB增大10倍)
                          "--enable-features=NetworkServiceInProcess");
    // A/B 测试 2D Canvas：默认尝试启用加速，可通过环境变量关闭
    if (qEnvironmentVariableIsSet("WEREAD_DISABLE_ACCEL_2D")) {
        chromiumFlags.append(" --disable-accelerated-2d-canvas");
        qInfo() << "[ENV] 2D canvas accel disabled (WEREAD_DISABLE_ACCEL_2D)";
    } else {
        chromiumFlags.append(" --enable-accelerated-2d-canvas");
        qInfo() << "[ENV] 2D canvas accel enabled (default, set WEREAD_DISABLE_ACCEL_2D to disable)";
    }
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromiumFlags);
    qInfo() << "[ENV] QTWEBENGINE_CHROMIUM_FLAGS" << qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTouchEvents);
    QCoreApplication::setAttribute(Qt::AA_SynthesizeTouchForUnhandledMouseEvents);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("weread-lab");
    QCoreApplication::setApplicationName("weread-browser");
    ShmFrameWriter writer;
    writer.init("/dev/shm/weread_frame");
    // Resolve start URL: env override > CLI arg > default shelf
    const QByteArray envUrl = qgetenv("WEREAD_URL");
    QUrl startUrl;
    if (qEnvironmentVariableIsSet("WEREAD_START_BLANK")) {
        startUrl = QUrl(QStringLiteral("about:blank"));
    } else if (!envUrl.isEmpty()) {
        startUrl = QUrl::fromUserInput(QString::fromLocal8Bit(envUrl));
    } else if (argc > 1) {
        startUrl = QUrl::fromUserInput(QString::fromLocal8Bit(argv[1]));
    } else {
        startUrl = QUrl(QStringLiteral("https://weread.qq.com/"));
    }
    qInfo() << "[WEREAD] Starting with URL:" << startUrl.toString();
    WereadBrowser window(startUrl, &writer);
    TouchBridge bridge(&window);
    window.show(); // even on offscreen platform, show() triggers painting
    window.raise();
    window.activateWindow();
    int result = app.exec();
    return result;
}

#include "main.moc"
