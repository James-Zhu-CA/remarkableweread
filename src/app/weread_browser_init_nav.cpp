#include "weread_browser.h"

void WereadBrowser::initNavigationSignals()
{
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
              m_view->setUrl(QUrl(fallback));
          }
          return;
      }
      if (toDedaoDetail && m_allowDedaoDetailOnce) {
          qInfo() << "[NAV_GUARD] allow dedao detail urlChanged once" << newUrlStr;
          m_allowDedaoDetailOnce = false;
      }
      logUrlState(QStringLiteral("urlChanged"));
      
      // 更新智能刷新管理器的书籍页面状态
      const bool isBook = isWeReadBook() || isDedaoBook();
      if (m_smartRefresh) {
          m_smartRefresh->setBookPage(isBook);
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
      
      if (isBook) {
          m_bookEnterTs = now;
          qInfo() << "[TIMING] bookEnter ts" << now << "url" << url;
          // 进入书籍时立即启动抓帧循环，避免需手动触发
          scheduleBookCaptures();
          // 书籍页提前注入章节观察器，避免早期请求漏掉
          if (isWeReadBook()) {
              installChapterObserver();
          }
          
          // 提前注入智能刷新 JS 监听器（从 urlChanged 开始，不等待 loadFinished）
          // 这样可以更早地捕获 DOM 变化和滚动事件
          const QString smartRefreshJs = QStringLiteral(R"(
  (function() {
  if (window.__SMART_REFRESH_INSTALLED__) {
  return;
  }
  window.__SMART_REFRESH_INSTALLED__ = true;
  
  let pendingEvents = [];
  let burstMode = false, burstTimeout = null;
  let lastScrollTop = 0;
  let lastReportTime = 0;
  
  // MutationObserver 检测 DOM 变化（优化：减少getBoundingClientRect调用，延迟处理区域计算）
  let pendingMutations = [];
  const processMutations = () => {
  if (pendingMutations.length === 0) return;
  const mutations = pendingMutations;
  pendingMutations = [];
  
  let score = 0;
  let regions = [];
  for (const m of mutations) {
      if (m.type === 'childList') {
          score += m.addedNodes.length * 10 + m.removedNodes.length * 10;
          // 优化：限制区域计算，只处理前5个节点，避免大量getBoundingClientRect调用影响点击响应
          const maxNodes = 5;
          let nodeCount = 0;
          for (const n of m.addedNodes) {
              if (nodeCount >= maxNodes) break;
              if (n.nodeType === 1 && n.getBoundingClientRect) {
                  try {
                      const r = n.getBoundingClientRect();
                      if (r.width > 0 && r.height > 0) {
                          regions.push({x:r.x|0, y:r.y|0, w:Math.ceil(r.width), h:Math.ceil(r.height)});
                          nodeCount++;
                      }
                  } catch(e) {}
              }
          }
      } else if (m.type === 'attributes') score += 2;
      else if (m.type === 'characterData') score += 3;
  }
  
  if (score > 0) {
      pendingEvents.push({t:'dom', s:score, r:regions});
  }
  };
  
  const observer = new MutationObserver((mutations) => {
  // 批量收集mutations，延迟处理以减少对点击响应的影响
  pendingMutations.push(...mutations);
  // 使用requestIdleCallback延迟处理，如果浏览器不支持则使用setTimeout
  if (window.requestIdleCallback) {
      requestIdleCallback(processMutations, {timeout: 50});
  } else {
      setTimeout(processMutations, 0);
  }
  });
  
  // 延迟启动观察器，等待 body 准备就绪
  const startObserver = () => {
  if (document.body) {
      observer.observe(document.body, {childList:true, subtree:true, attributes:true, characterData:true});
  } else {
      setTimeout(startObserver, 100);
  }
  };
  startObserver();
  
  // 滚动监听（实时检测，节流 30ms 以降低频率但保持响应性）
  let scrollTimer = null;
  window.addEventListener('scroll', () => {
  if (scrollTimer) return;
  scrollTimer = setTimeout(() => {
      const el = document.scrollingElement || document.documentElement;
      const delta = Math.abs(el.scrollTop - lastScrollTop);
      lastScrollTop = el.scrollTop;
      if (delta > 10) {
          pendingEvents.push({t:'scroll', d:delta});
          // 实时汇报：如果距离上次汇报超过 50ms，立即发送（不等待批量窗口）
          const now = Date.now();
          if (now - lastReportTime > 50) {
              console.log('[REFRESH_EVENTS]' + JSON.stringify(pendingEvents));
              pendingEvents = [];
              lastReportTime = now;
          }
      }
      scrollTimer = null;
  }, 30);  // 降低节流时间到 30ms，提高响应性
  }, {passive:true});
  
  // 100ms 批量汇报（作为兜底，确保小事件也能被处理）
  // 书籍页面已禁用burstMode，所以移除!burstMode条件
  setInterval(() => {
  if (pendingEvents.length > 0) {
      console.log('[REFRESH_EVENTS]' + JSON.stringify(pendingEvents));
      pendingEvents = [];
      lastReportTime = Date.now();
  }
  }, 100);
  
  })();
  )");
          // 延迟 100ms 注入，确保页面开始加载
          QTimer::singleShot(100, this, [this, smartRefreshJs]() {
              if (m_view && m_view->page()) {
                  m_view->page()->runJavaScript(smartRefreshJs);
                  qInfo() << "[SMART_REFRESH] Injected JS monitor script from urlChanged (early injection)";
                  
                  // 同时注入自动重试监听器
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
  });
  // 允许通过环境变量加载本地保存的 wr.html 以验证解析差异
}
