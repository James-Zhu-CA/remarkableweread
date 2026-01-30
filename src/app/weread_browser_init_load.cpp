#include "weread_browser.h"
#include <QWebEngineLoadingInfo>

void WereadBrowser::initLoadSignals()
{
  connect(m_view, &QWebEngineView::loadStarted, this, [this](){
      const qint64 ts = QDateTime::currentMSecsSinceEpoch();
      const qint64 reasonAge =
          m_lastNavReasonTs > 0 ? (ts - m_lastNavReasonTs) : -1;
      const QUrl viewUrl = m_view ? m_view->url() : QUrl();
      const QUrl pageUrl =
          (m_view && m_view->page()) ? m_view->page()->url() : QUrl();
      const QUrl requestedUrl =
          (m_view && m_view->page()) ? m_view->page()->requestedUrl() : QUrl();
      qInfo() << "[TIMING] loadStarted url" << currentUrl << "ts" << ts
              << "viewUrl" << viewUrl << "pageUrl" << pageUrl
              << "requestedUrl" << requestedUrl
              << "reason" << m_lastNavReason
              << "reasonTarget" << m_lastNavReasonTarget
              << "reasonAgeMs" << reasonAge;
      m_bookEarlyReloads = 0;
      m_iframeFixApplied = false;
      m_diagRan = false;
      m_contentReadyTriggered = false;
      // 注意：cookie 设置已在 urlChanged 时完成，这里不再重复设置
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

  connect(m_view->page(), &QWebEnginePage::loadStarted, this, [this]() {
      const qint64 ts = QDateTime::currentMSecsSinceEpoch();
      const qint64 reasonAge =
          m_lastNavReasonTs > 0 ? (ts - m_lastNavReasonTs) : -1;
      const QUrl viewUrl = m_view ? m_view->url() : QUrl();
      const QUrl pageUrl =
          (m_view && m_view->page()) ? m_view->page()->url() : QUrl();
      const QUrl requestedUrl =
          (m_view && m_view->page()) ? m_view->page()->requestedUrl() : QUrl();
      qInfo() << "[PAGE] loadStarted"
              << "ts" << ts << "viewUrl" << viewUrl << "pageUrl" << pageUrl
              << "requestedUrl" << requestedUrl
              << "reason" << m_lastNavReason
              << "reasonTarget" << m_lastNavReasonTarget
              << "reasonAgeMs" << reasonAge;
      logHistoryState(QStringLiteral("pageLoadStarted"));
  });

  connect(m_view->page(), &QWebEnginePage::loadFinished, this,
          [this](bool ok) {
      const qint64 ts = QDateTime::currentMSecsSinceEpoch();
      const qint64 reasonAge =
          m_lastNavReasonTs > 0 ? (ts - m_lastNavReasonTs) : -1;
      const QUrl viewUrl = m_view ? m_view->url() : QUrl();
      const QUrl pageUrl =
          (m_view && m_view->page()) ? m_view->page()->url() : QUrl();
      const QUrl requestedUrl =
          (m_view && m_view->page()) ? m_view->page()->requestedUrl() : QUrl();
      qInfo() << "[PAGE] loadFinished"
              << "ok" << ok << "ts" << ts << "viewUrl" << viewUrl
              << "pageUrl" << pageUrl << "requestedUrl" << requestedUrl
              << "reason" << m_lastNavReason
              << "reasonTarget" << m_lastNavReasonTarget
              << "reasonAgeMs" << reasonAge;
      logHistoryState(QStringLiteral("pageLoadFinished"));
  });

  connect(m_view->page(), &QWebEnginePage::loadingChanged, this,
          [this](const QWebEngineLoadingInfo &info) {
      const qint64 ts = QDateTime::currentMSecsSinceEpoch();
      const qint64 reasonAge =
          m_lastNavReasonTs > 0 ? (ts - m_lastNavReasonTs) : -1;
      const QUrl viewUrl = m_view ? m_view->url() : QUrl();
      const QUrl pageUrl =
          (m_view && m_view->page()) ? m_view->page()->url() : QUrl();
      const QUrl requestedUrl =
          (m_view && m_view->page()) ? m_view->page()->requestedUrl() : QUrl();
      const auto status = info.status();
      const char *statusStr = "unknown";
      switch (status) {
      case QWebEngineLoadingInfo::LoadStartedStatus:
        statusStr = "started";
        break;
      case QWebEngineLoadingInfo::LoadStoppedStatus:
        statusStr = "stopped";
        break;
      case QWebEngineLoadingInfo::LoadSucceededStatus:
        statusStr = "succeeded";
        break;
      case QWebEngineLoadingInfo::LoadFailedStatus:
        statusStr = "failed";
        break;
      }
      const auto domain = info.errorDomain();
      const char *domainStr = "unknown";
      switch (domain) {
      case QWebEngineLoadingInfo::NoErrorDomain:
        domainStr = "none";
        break;
      case QWebEngineLoadingInfo::InternalErrorDomain:
        domainStr = "internal";
        break;
      case QWebEngineLoadingInfo::ConnectionErrorDomain:
        domainStr = "connection";
        break;
      case QWebEngineLoadingInfo::CertificateErrorDomain:
        domainStr = "certificate";
        break;
      case QWebEngineLoadingInfo::HttpErrorDomain:
        domainStr = "http";
        break;
      case QWebEngineLoadingInfo::FtpErrorDomain:
        domainStr = "ftp";
        break;
      case QWebEngineLoadingInfo::DnsErrorDomain:
        domainStr = "dns";
        break;
      case QWebEngineLoadingInfo::HttpStatusCodeDomain:
        domainStr = "http_status";
        break;
      }
      qInfo() << "[TIMING] loadingChanged"
              << "status" << statusStr
              << "url" << info.url()
              << "isErrorPage" << info.isErrorPage()
              << "errorDomain" << domainStr
              << "errorCode" << info.errorCode()
              << "errorString" << info.errorString()
              << "ts" << ts
              << "viewUrl" << viewUrl
              << "pageUrl" << pageUrl
              << "requestedUrl" << requestedUrl
              << "reason" << m_lastNavReason
              << "reasonTarget" << m_lastNavReasonTarget
              << "reasonAgeMs" << reasonAge;
      if (status == QWebEngineLoadingInfo::LoadSucceededStatus) {
        if (m_dedaoCatalogJumpPending && isDedaoBook() && m_smartRefreshDedao) {
          qInfo() << "[CATALOG_DEDAO] load succeeded after jump, refresh";
          m_smartRefreshDedao->triggerDedaoDuRefresh(
              QStringLiteral("catalog_nav"));
          m_smartRefreshDedao->scheduleDedaoFallbackSeries();
          m_dedaoCatalogJumpPending = false;
        }
      }
      logHistoryState(QStringLiteral("loadingChanged"));
  });

  connect(m_view->page(), &QWebEnginePage::renderProcessTerminated, this,
          [this](QWebEnginePage::RenderProcessTerminationStatus status,
                 int exitCode) {
      const qint64 ts = QDateTime::currentMSecsSinceEpoch();
      const QUrl viewUrl = m_view ? m_view->url() : QUrl();
      const QUrl pageUrl =
          (m_view && m_view->page()) ? m_view->page()->url() : QUrl();
      qInfo() << "[PROC] renderProcessTerminated"
              << "status" << status
              << "exitCode" << exitCode
              << "ts" << ts
              << "viewUrl" << viewUrl
              << "pageUrl" << pageUrl;
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
          if (isDedaoSite() && !isDedaoBook()) {
              const QUrl viewUrl = m_view ? m_view->url() : QUrl();
              const QString host = viewUrl.host();
              const QString path = viewUrl.path();
              const bool isHome =
                  host.endsWith(QStringLiteral("dedao.cn"),
                                Qt::CaseInsensitive) &&
                  (path.isEmpty() || path == QStringLiteral("/"));
              if (isHome) {
                  const QString js = QStringLiteral(R"JS(
(() => {
  if (!/dedao\.cn$/i.test(location.host || '')) return {ok:false, reason:'not-dedao-host'};
  const path = location.pathname || '';
  if (path && path !== '/') return {ok:false, reason:'not-home', path};
  let setting = {};
  let updated = false;
  let raw = '';
  try { raw = localStorage.getItem('readerSettings') || ''; } catch(e) { return {ok:false, reason:'localStorage-error', error:String(e)}; }
  if (raw) {
    try {
      const parsed = JSON.parse(raw);
      if (parsed && typeof parsed === 'object') setting = parsed;
    } catch(e) { setting = {}; }
  }
  const before = {fontLevel: setting.fontLevel, fontFamily: setting.fontFamily};
  const rawLevel = setting.fontLevel;
  let levelNum = null;
  if (rawLevel !== null && rawLevel !== undefined && rawLevel !== '') {
    const parsed = parseInt(rawLevel, 10);
    if (!isNaN(parsed)) levelNum = parsed;
  }
  if (levelNum === null || levelNum < 1) {
    setting.fontLevel = 7;
    updated = true;
  }
  const currentFont = (setting.fontFamily || '').trim();
  const currentLower = currentFont.toLowerCase();
  const fontInvalid = !currentFont ||
                      currentLower == "default" ||
                      currentLower == "system" ||
                      currentLower == "xitongziti";
  if (fontInvalid) {
    setting.fontFamily = 'HanYiQiHei';
    updated = true;
  }
  if (updated) {
    try { localStorage.setItem('readerSettings', JSON.stringify(setting)); }
    catch(e) { return {ok:false, reason:'write-failed', error:String(e)}; }
  }
  return {ok:true, updated, before, after:{fontLevel: setting.fontLevel, fontFamily: setting.fontFamily}};
})()
)JS");
                  m_view->page()->runJavaScript(
                      js, [](const QVariant &res) {
                        qInfo() << "[DEDAO_HOME_SETTINGS]" << res;
                      });
              }
          }
          if (isDedaoSite()) {
              scheduleDedaoUiFixes();
          }
          scheduleWeReadUiFixes();

          if (isWeReadBook() || isDedaoBook()) {
              QTimer::singleShot(800, this, [this]() {
                  logComputedFontOnce();
              });
          }
          
          // 智能延迟重试：仅在书籍页面且未调度过检查时，延迟检查一次（低功耗）
          if ((isWeReadBook() || isDedaoBook()) && !m_contentCheckScheduled) {
              m_contentCheckScheduled = true;
              m_contentRetryCount = 0;  // 重置计数器
              qInfo() << "[RETRY] Scheduling content check after" << CONTENT_CHECK_DELAY_MS << "ms";
              QTimer::singleShot(CONTENT_CHECK_DELAY_MS, this, [this]() {
                  checkContentAndRetryIfNeeded();
                  m_contentCheckScheduled = false;  // 允许下次页面加载时再次检查
              });
          }
          
          // 恢复滚动位置（如果是恢复的会话）
          if (m_restoredScrollY > 0) {
              QTimer::singleShot(500, this, [this]() {
                  if (m_view && m_view->page()) {
                      m_view->page()->runJavaScript(
                          QStringLiteral("window.scrollTo(0, %1)").arg(m_restoredScrollY)
                      );
                      qInfo() << "[SESSION] Restored scroll position:" << m_restoredScrollY;
                  }
                  m_restoredScrollY = 0;  // 清除，避免后续页面也恢复
                  m_isRestoringSession = false;  // 恢复完成
              });
          }
          
          // 全屏刷新由智能刷新管理器统一处理，避免重复刷新
          // 智能刷新管理器会在 JS 监听器注入后通过 triggerLoadFinished() 触发
  
          // Phase 3: Inject performance monitoring JavaScript
  const bool enablePerfMonitor =
      qEnvironmentVariableIsSet("WEREAD_PERF_MONITOR") &&
      qEnvironmentVariableIntValue("WEREAD_PERF_MONITOR") != 0;
  if (enablePerfMonitor) {
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
  }
  
          const bool isWeReadBookPage = isWeReadBook();
          const bool isDedaoBookPage = isDedaoBook();
          const bool isDedaoSitePage = isDedaoSite();
          // Phase 4: 注入智能刷新 DOM 监听器（如果 urlChanged 中已注入则跳过）
          if (isWeReadBookPage || isDedaoSitePage) {
              QString smartRefreshFlag;
              if (isWeReadBookPage) {
                  smartRefreshFlag =
                      QStringLiteral("__SMART_REFRESH_WR_INSTALLED__");
              } else if (isDedaoBookPage) {
                  smartRefreshFlag = QStringLiteral(
                      "__SMART_REFRESH_DEDAO_READER_INSTALLED__");
              } else {
                  smartRefreshFlag =
                      QStringLiteral("__SMART_REFRESH_DEDAO_SITE_INSTALLED__");
              }
              installSmartRefreshScript(smartRefreshFlag);
              const QString smartRefreshJs =
                  buildSmartRefreshScript(smartRefreshFlag);
              m_view->page()->runJavaScript(smartRefreshJs);
              qInfo() << "[SMART_REFRESH] Injected JS monitor script "
                         "(loadFinished)"
                      << smartRefreshFlag;
          }
          
          if (isWeReadBookPage) {
              // Phase 5: 注入网络请求自动重试监听器（不重新加载页面，仅微信读书）
              const QString autoRetryJs = QStringLiteral(R"(
  (function() {
  if (window.__WR_AUTO_RETRY_INSTALLED__) return;
  window.__WR_AUTO_RETRY_INSTALLED__ = true;
  
  // 记录 API 调用（用于手动重试）
  const apiCalls = [];
  const originalFetch = window.fetch;
  
  // 拦截 fetch，自动重试失败的 /web/book/read 请求
  window.fetch = function(...args) {
  const url = args[0];
  if (typeof url === 'string' && url.includes('/web/book/read')) {
      // 记录API调用信息
      apiCalls.push({
          url: url,
          options: args[1] || {},
          timestamp: Date.now()
      });
      // 自动重试失败的请求（延迟2秒）
      return originalFetch.apply(this, args).catch(err => {
          console.log('[RETRY] Book read API failed, auto-retrying in 2s...', err);
          return new Promise((resolve, reject) => {
              setTimeout(() => {
                  originalFetch.apply(this, args)
                      .then(resolve)
                      .catch(reject);
              }, 2000);
          });
      });
  }
  return originalFetch.apply(this, args);
  };
  
  // 拦截 XMLHttpRequest（如果页面使用 XHR）
  const originalXHROpen = XMLHttpRequest.prototype.open;
  const originalXHRSend = XMLHttpRequest.prototype.send;
  
  XMLHttpRequest.prototype.open = function(method, url, ...rest) {
  this._method = method;
  this._url = url;
  return originalXHROpen.apply(this, [method, url, ...rest]);
  };
  
  XMLHttpRequest.prototype.send = function(...args) {
  if (this._url && this._url.includes('/web/book/read')) {
      this.addEventListener('error', function() {
          console.log('[RETRY] XHR book read failed, auto-retrying...');
          setTimeout(() => {
              const retryXHR = new XMLHttpRequest();
              retryXHR.open(this._method || 'GET', this._url);
              for (let key in this) {
                  if (key.startsWith('setRequestHeader')) {
                      try {
                          const headers = this.getAllResponseHeaders();
                          if (headers) {
                              headers.split('\r\n').forEach(line => {
                                  const [name, value] = line.split(': ');
                                  if (name && value) retryXHR.setRequestHeader(name, value);
                              });
                          }
                      } catch(e) {}
                  }
              }
              retryXHR.send(...args);
          }, 2000);
      });
  }
  return originalXHRSend.apply(this, args);
  };
  
  // 暴露手动重试函数（供C++代码调用）
  window.__WR_RETRY_BOOK_READ__ = function() {
  if (apiCalls.length > 0) {
      const lastCall = apiCalls[apiCalls.length - 1];
      console.log('[RETRY] Manual retry triggered, using recorded API call:', lastCall);
      return originalFetch(lastCall.url, lastCall.options).catch(err => {
          console.error('[RETRY] Manual retry failed:', err);
          throw err;
      });
  } else {
      // 如果没有记录，尝试通用方式
      const bookId = (window.location.href || '').match(/reader\/([a-zA-Z0-9]+)/)?.[1];
      if (bookId) {
          console.log('[RETRY] Manual retry triggered, using fallback method, bookId:', bookId);
          return originalFetch(`https://weread.qq.com/web/book/read?bookId=${bookId}`, {
              credentials: 'include',
              headers: {'Content-Type': 'application/json'}
          });
      }
      return Promise.reject('no_api_call_recorded_and_no_bookId');
  }
  };
  
  console.log('[RETRY] Auto-retry listener installed');
  })();
  )");
          m_view->page()->runJavaScript(autoRetryJs);
          qInfo() << "[SMART_REFRESH] Injected auto-retry listener (loadFinished)";
          }
  
          // 通知智能刷新管理器页面加载完成
          if (SmartRefreshManager *mgr = smartRefreshForPage()) {
              mgr->triggerLoadFinished();
          }
  
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
                  const QString href = m.value(QStringLiteral("href")).toString();
                  if (m_integrityReloads < 2 && (bodyLen < 200 && !hasNuxt)) {
                      m_integrityReloads++;
                      qWarning() << "[INTEGRITY] fail bodyLen" << bodyLen << "hasNuxt" << hasNuxt
                                 << "reload bypass cache attempt" << m_integrityReloads
                                 << "href" << href << "url" << currentUrl;
                      recordNavReason(QStringLiteral("integrity_reload"),
                                      currentUrl);
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
                                 << "frames" << m.value(QStringLiteral("frames")).toInt()
                                 << "url" << currentUrl;
                      recordNavReason(QStringLiteral("book_empty_reload"),
                                      currentUrl);
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
              // 书籍页需注入章节观察器
              installChapterObserver();
  
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
          // 应用书籍页样式修复和视觉诊断（替代 Ready 机制）
          applyBookPageFixes();
      }
      else { qWarning() << "[WEREAD] Page load failed"; }
  });
  // 导航拦截：在真正发起请求前阻止误跳转到得到详情页
}
