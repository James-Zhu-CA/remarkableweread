#include "weread_browser.h"

void WereadBrowser::initNavigationSignals()
{
  connect(m_view->page(), &QWebEnginePage::navigationRequested, this,
          [this](QWebEngineNavigationRequest &request) {
      const QString target = request.url().toString();
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      const qint64 reasonAge =
          m_lastNavReasonTs > 0 ? (now - m_lastNavReasonTs) : -1;
      const QUrl pageUrl =
          (m_view && m_view->page()) ? m_view->page()->url() : QUrl();
      const QUrl requestedUrl =
          (m_view && m_view->page()) ? m_view->page()->requestedUrl() : QUrl();
      qInfo() << "[NAV_REQ]"
              << "url" << target
              << "type" << request.navigationType()
              << "mainFrame" << request.isMainFrame()
              << "hasFormData" << request.hasFormData()
              << "from" << currentUrl
              << "pageUrl" << pageUrl
              << "requestedUrl" << requestedUrl
              << "reason" << m_lastNavReason
              << "reasonTarget" << m_lastNavReasonTarget
              << "reasonAgeMs" << reasonAge;
      const bool isReload =
          request.navigationType() ==
          QWebEngineNavigationRequest::ReloadNavigation;
      const bool isMenuToggle =
          m_lastNavReason == QStringLiteral("menu_toggle_service");
      const QString targetStr = request.url().toString();
      const bool isWeReadHome =
          targetStr == QStringLiteral("https://weread.qq.com/") ||
          targetStr == QStringLiteral("https://weread.qq.com");
      const bool targetIsDedao =
          m_lastNavReasonTarget.toString().contains(
              QStringLiteral("dedao.cn"));
      if (request.isMainFrame() && isReload && isMenuToggle && targetIsDedao &&
          isWeReadHome && reasonAge >= 0 && reasonAge <= 2000) {
          qWarning() << "[NAV_GUARD] block reload to weread after menu toggle"
                     << "url" << targetStr << "reasonAgeMs" << reasonAge;
          request.reject();
          return;
      }
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

  connect(m_view->page(), &QWebEnginePage::urlChanged, this,
          [this](const QUrl &url) {
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      const qint64 reasonAge =
          m_lastNavReasonTs > 0 ? (now - m_lastNavReasonTs) : -1;
      const QUrl viewUrl = m_view ? m_view->url() : QUrl();
      const QUrl pageUrl =
          (m_view && m_view->page()) ? m_view->page()->url() : QUrl();
      const QUrl requestedUrl =
          (m_view && m_view->page()) ? m_view->page()->requestedUrl() : QUrl();
      qInfo() << "[PAGE] urlChanged" << url << "ts" << now
              << "viewUrl" << viewUrl
              << "pageUrl" << pageUrl
              << "requestedUrl" << requestedUrl
              << "reason" << m_lastNavReason
              << "reasonTarget" << m_lastNavReasonTarget
              << "reasonAgeMs" << reasonAge;
      logHistoryState(QStringLiteral("pageUrlChanged"));
  });

  connect(m_view->page(), &QWebEnginePage::titleChanged, this,
          [this](const QString &title) {
      const QUrl pageUrl =
          (m_view && m_view->page()) ? m_view->page()->url() : QUrl();
      qInfo() << "[PAGE] titleChanged" << title << "pageUrl" << pageUrl;
  });

  connect(m_view->page(), &QWebEnginePage::iconUrlChanged, this,
          [this](const QUrl &iconUrl) {
      const QUrl pageUrl =
          (m_view && m_view->page()) ? m_view->page()->url() : QUrl();
      qInfo() << "[PAGE] iconUrlChanged" << iconUrl << "pageUrl" << pageUrl;
  });
  
  connect(m_view, &QWebEngineView::urlChanged, this, [this](const QUrl &url){
      const QString oldUrl = currentUrl.toString();
      const QString newUrlStr = url.toString();
      currentUrl = url;
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      const qint64 reasonAge =
          m_lastNavReasonTs > 0 ? (now - m_lastNavReasonTs) : -1;
      const QUrl pageUrl =
          (m_view && m_view->page()) ? m_view->page()->url() : QUrl();
      const QUrl requestedUrl =
          (m_view && m_view->page()) ? m_view->page()->requestedUrl() : QUrl();
      qInfo() << "[TIMING] urlChanged" << url << "ts" << now << "from" << oldUrl
              << "pageUrl" << pageUrl << "requestedUrl" << requestedUrl
              << "reason" << m_lastNavReason
              << "reasonTarget" << m_lastNavReasonTarget
              << "reasonAgeMs" << reasonAge;
      
      // 重置内容重试状态（URL变化时）
      m_contentRetryCount = 0;
      m_contentCheckScheduled = false;
      m_lastContentCheckTime = 0;
      m_lastContentRetryTime = 0;
      m_contentFirstCheckFailed = false;
      
      // 记录最新的得到阅读页，便于回退
      if (newUrlStr.contains(QStringLiteral("dedao.cn")) &&
          newUrlStr.contains(QStringLiteral("/ebook/reader"))) {
          m_lastDedaoReaderUrl = newUrlStr;
      }
      
      // 延迟保存会话状态（避免频繁写入）
      if (!m_isRestoringSession) {
          QTimer::singleShot(2000, this, [this]() {
              saveSessionState();
          });
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
              recordNavReason(QStringLiteral("dedao_detail_fallback"),
                              QUrl(fallback));
              m_view->setUrl(QUrl(fallback));
          }
          return;
      }
      if (toDedaoDetail && m_allowDedaoDetailOnce) {
          qInfo() << "[NAV_GUARD] allow dedao detail urlChanged once" << newUrlStr;
          m_allowDedaoDetailOnce = false;
      }
      logUrlState(QStringLiteral("urlChanged"));
      logHistoryState(QStringLiteral("viewUrlChanged"));
      
      // 更新智能刷新管理器的书籍页面状态（按站点拆分）
      const bool isWeReadBookPage = isWeReadBook();
      const bool isDedaoBookPage = isDedaoBook();
      const bool isDedaoSitePage = isDedaoSite();
      if (m_smartRefreshWeRead) {
          m_smartRefreshWeRead->setBookPage(isWeReadBookPage);
      }
      if (m_smartRefreshDedao) {
          m_smartRefreshDedao->setBookPage(isDedaoBookPage);
      }
      
      // 在urlChanged时设置cookie（如果是微信读书域名，包括首页和书籍页面）
      // 这样无论是首页还是书籍页面，cookie都会提前设置好
      const bool isWeReadDomain = newUrlStr.contains(QStringLiteral("weread.qq.com"));
      if (isWeReadDomain && m_view && m_view->page() && m_view->page()->profile()) {
          QNetworkCookie cookie(QByteArray("wr_theme"), QByteArray("white"));
          cookie.setDomain(".weread.qq.com");
          cookie.setPath("/");
          cookie.setExpirationDate(QDateTime::currentDateTime().addYears(1));
          m_view->page()->profile()->cookieStore()->setCookie(cookie, QUrl("https://weread.qq.com"));
          qInfo() << "[DEFAULT] Set theme cookie to white via CookieStore (urlChanged, url=" << newUrlStr << ")";
      }
      
      if (isWeReadBookPage || isDedaoBookPage) {
          m_bookEnterTs = now;
          qInfo() << "[TIMING] bookEnter ts" << now << "url" << url;
          // 进入书籍时立即启动抓帧循环，避免需手动触发
          scheduleBookCaptures();
          // 书籍页提前注入章节观察器，避免早期请求漏掉
          if (isWeReadBook()) {
              installChapterObserver();
          }
      }

      if (isWeReadBookPage || isDedaoSitePage) {
          // 提前注入智能刷新 JS 监听器（从 urlChanged 开始，不等待 loadFinished）
          // 这样可以更早地捕获 DOM 变化和滚动事件
          QString smartRefreshFlag;
          if (isWeReadBookPage) {
              smartRefreshFlag = QStringLiteral("__SMART_REFRESH_WR_INSTALLED__");
          } else if (isDedaoBookPage) {
              smartRefreshFlag =
                  QStringLiteral("__SMART_REFRESH_DEDAO_READER_INSTALLED__");
          } else {
              smartRefreshFlag =
                  QStringLiteral("__SMART_REFRESH_DEDAO_SITE_INSTALLED__");
          }
          installSmartRefreshScript(smartRefreshFlag);
          const QString smartRefreshJs = buildSmartRefreshScript(smartRefreshFlag);
          const bool injectWeReadRetry = isWeReadBookPage;
          // 延迟 100ms 注入，确保页面开始加载
          QTimer::singleShot(100, this,
                             [this, smartRefreshJs, smartRefreshFlag,
                              injectWeReadRetry]() {
              if (m_view && m_view->page()) {
                  m_view->page()->runJavaScript(smartRefreshJs);
                  qInfo() << "[SMART_REFRESH] Injected JS monitor script from "
                             "urlChanged (early injection)"
                          << smartRefreshFlag;
                  
                  if (!injectWeReadRetry) {
                      return;
                  }

                  // 同时注入自动重试监听器（仅微信读书）
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
  
  console.log('[RETRY] Auto-retry listener installed (from urlChanged)');
  })();
  )");
                  m_view->page()->runJavaScript(autoRetryJs);
              }
          });
      }
      updateUserAgentForUrl(url);
      sendBookState();
      if (isDedaoSite()) {
        scheduleDedaoUiFixes();
      }
      scheduleWeReadUiFixes();
  });
  // 允许通过环境变量加载本地保存的 wr.html 以验证解析差异
}
