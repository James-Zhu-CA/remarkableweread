#include "weread_browser.h"
#include <QTextStream>

// Scripts moved to weread_browser_scripts.cpp

void WereadBrowser::installChapterObserverScript() {
  if (!m_view || !m_view->page())
    return;
  if (m_chapterObserverScriptInstalled)
    return;
  QWebEngineScript script;
  script.setName(QStringLiteral("weread-chapter-observer"));
  script.setInjectionPoint(QWebEngineScript::DocumentCreation);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setRunsOnSubFrames(true);
  script.setSourceCode(getChapterObserverScript());
  m_view->page()->scripts().insert(script);
  m_chapterObserverScriptInstalled = true;
  qInfo() << "[OBS] script installed";
}

void WereadBrowser::installWeReadDefaultSettingsScript() {
  if (!m_view || !m_view->page())
    return;
  if (m_defaultSettingsScriptInstalled)
    return;
  QWebEngineScript script;
  script.setName(QStringLiteral("weread-default-settings"));
  script.setInjectionPoint(QWebEngineScript::DocumentCreation);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setRunsOnSubFrames(true);
  script.setSourceCode(getDefaultSettingsScript());
  m_view->page()->scripts().insert(script);
  m_defaultSettingsScriptInstalled = true;
  qInfo() << "[DEFAULTS] script installed";
}

void WereadBrowser::installDedaoDefaultSettingsScript() {
  if (!m_view || !m_view->page())
    return;
  if (m_dedaoDefaultSettingsScriptInstalled)
    return;
  QWebEngineScript script;
  script.setName(QStringLiteral("dedao-default-settings"));
  script.setInjectionPoint(QWebEngineScript::DocumentCreation);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setRunsOnSubFrames(true);
  script.setSourceCode(getDedaoDefaultSettingsScript());
  m_view->page()->scripts().insert(script);
  m_dedaoDefaultSettingsScriptInstalled = true;
  qInfo() << "[DEDAO_DEFAULTS] script installed";
}

void WereadBrowser::installSmartRefreshScript(const QString &flagName) {
  if (!m_view || !m_view->page())
    return;
  const QString scriptName = QStringLiteral("smart-refresh-") + flagName;
  const QList<QWebEngineScript> existing =
      m_view->page()->scripts().find(scriptName);
  if (!existing.isEmpty())
    return;
  QWebEngineScript script;
  script.setName(scriptName);
  script.setInjectionPoint(QWebEngineScript::DocumentCreation);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setRunsOnSubFrames(true);
  script.setSourceCode(buildSmartRefreshScript(flagName));
  m_view->page()->scripts().insert(script);
  qInfo() << "[SMART_REFRESH] script installed" << scriptName;
}

void WereadBrowser::installChapterObserver() {
  if (!m_view || !m_view->page())
    return;
  const bool isBook =
      currentUrl.toString().contains(QStringLiteral("/web/reader/"));
  if (!isBook)
    return;
  installChapterObserverScript();
  m_view->page()->runJavaScript(
      getChapterObserverScript(),
      [](const QVariant &res) { qInfo() << "[OBS] install" << res; });
}

void WereadBrowser::cycleUserAgentMode() {
  static const QStringList modes = {QStringLiteral("default"),
                                    QStringLiteral("android"),
                                    QStringLiteral("kindle")};
  static int idx = 0;
  idx = (idx + 1) % modes.size();
  const QString mode = modes.at(idx);
  // 依据模式选择 UA，并同时应用到微信/非微信页面，随后强制重载
  const QString uaDefault = m_profile ? m_profile->httpUserAgent() : QString();
  const QString kindleUA = QStringLiteral(
      "Mozilla/5.0 (Linux; Kindle Paperwhite) AppleWebKit/537.36 (KHTML, "
      "like Gecko) Silk/3.2 Mobile Safari/537.36");
  const QString androidUA = QStringLiteral(
      "Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, "
      "like Gecko) Chrome/129.0.0.0 Mobile Safari/537.36");
  const QString iosSafariUA =
      QStringLiteral("Mozilla/5.0 (iPhone; CPU iPhone OS 17_1 like Mac OS X) "
                     "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 "
                     "Mobile/15E148 Safari/604.1");

  QString targetUA = iosSafariUA; // default
  if (mode.compare(QStringLiteral("kindle"), Qt::CaseInsensitive) == 0) {
    targetUA = kindleUA;
  } else if (mode.compare(QStringLiteral("android"), Qt::CaseInsensitive) ==
             0) {
    targetUA = androidUA;
  } else if (mode.compare(QStringLiteral("default"), Qt::CaseInsensitive) ==
                 0 &&
             !uaDefault.isEmpty()) {
    targetUA = uaDefault;
  }

  // 记录环境变量，便于外部脚本感知
  qputenv("WEREAD_UA_MODE", mode.toUtf8());

  // 同步更新 UA 配置：仅影响微信读书/非微信页面，得到保持独立 UA
  m_uaNonWeRead = targetUA;
  m_uaWeReadBook = targetUA;
  m_currentUA = QStringLiteral("unset"); // 触发后续 update 生效

  // 取消待执行的抓帧，准备重载
  cancelPendingCaptures();
  m_firstFrameDone = false;
  m_reloadAttempts = 0;

  // 应用新的 UA 并强制重载当前 URL
  updateUserAgentForUrl(currentUrl);
  // 以实际生效的 UA 回填模式
  m_uaMode = detectUaMode(m_profile ? m_profile->httpUserAgent() : targetUA);
  m_lastReloadTs = QDateTime::currentMSecsSinceEpoch();
  qInfo() << "[UA] cycle to" << m_uaMode << "UA:" << targetUA << "reload"
          << currentUrl << "ts" << m_lastReloadTs;
  if (m_view) {
    recordNavReason(QStringLiteral("ua_cycle_reload"), currentUrl);
    m_view->load(currentUrl);
  }
}

void WereadBrowser::logUrlState(const QString &tag) {
  const QString u = currentUrl.toString();
  qInfo() << "[URL]" << tag << u << "isWeReadBook" << isWeReadBook()
          << "isDedao" << isDedaoBook();
}

void WereadBrowser::logHistoryState(const QString &tag) {
  if (!m_view || !m_view->page())
    return;
  auto *hist = m_view->page()->history();
  if (!hist) {
    qInfo() << "[HISTORY]" << tag << "none";
    return;
  }
  const int count = hist->count();
  const int idx = hist->currentItemIndex();
  const bool canBack = hist->canGoBack();
  const bool canForward = hist->canGoForward();
  const QUrl currentItemUrl = hist->currentItem().url();
  const QUrl backItemUrl = canBack ? hist->backItem().url() : QUrl();
  const QUrl forwardItemUrl = canForward ? hist->forwardItem().url() : QUrl();
  qInfo() << "[HISTORY]" << tag << "count" << count << "index" << idx
          << "canBack" << canBack << "canForward" << canForward
          << "current" << currentItemUrl << "back" << backItemUrl
          << "forward" << forwardItemUrl;
}

void WereadBrowser::logPageProbe(const QString &tag) {
  if (!m_view || !m_view->page())
    return;
  const QString js = QStringLiteral(
      "(() => {"
      "  const url = location.href || '';"
      "  const host = location.host || '';"
      "  const path = location.pathname || '';"
      "  const cont = document.querySelector('.renderTargetContent');"
      "  const docEl = document.documentElement;"
      "  const scrollEl = document.scrollingElement || docEl || "
      "document.body;"
      "  const info = {"
      "    url, host, path,"
      "    hasRenderTarget: !!cont,"
      "    scrollElTag: scrollEl ? scrollEl.tagName : '',"
      "    scrollH: scrollEl ? scrollEl.scrollHeight : 0,"
      "    clientH: scrollEl ? scrollEl.clientHeight : 0,"
      "    offsetH: scrollEl ? scrollEl.offsetHeight : 0,"
      "  };"
      "  return info;"
      "})()");
  m_view->page()->runJavaScript(
      js, [tag](const QVariant &v) { qInfo() << "[PROBE]" << tag << v; });
}

QString WereadBrowser::buildSmartRefreshScript(const QString &flagName) const {
  const QString js = QStringLiteral(R"WR(
(function() {
  const host = String(location.host || '');
  const path = String(location.pathname || '');
  const isDedaoSite = (host === 'dedao.cn' || host.endsWith('.dedao.cn'));
  const isDedaoReader = isDedaoSite && path.indexOf('/ebook/reader') !== -1;
  const isWeRead = (host === 'weread.qq.com' || host.endsWith('.weread.qq.com')) &&
                   path.indexOf('/web/reader/') !== -1;
  const shouldRun = %2;
  if (!shouldRun) {
  return;
  }
  if (window.%1) {
  return;
  }
  window.%1 = true;

  let pendingEvents = [];
  let burstMode = false, burstTimeout = null;
  let lastReportTime = 0;
  const sendTrace = (reason, extra) => {
  const payload = Object.assign({
      t:'trace', host:host, path:path, flag:'%1',
      dedao:isDedaoSite, weread:isWeRead, reason:reason || ''
  }, extra || {});
  pendingEvents.push(payload);
  console.log('[REFRESH_EVENTS]' + JSON.stringify(pendingEvents));
  pendingEvents = [];
  lastReportTime = Date.now();
  };
  sendTrace('install');

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
  if (isWeRead || (isDedaoSite && !isDedaoReader)) {
  startObserver();
  }
  
  const getScrollEl = () => {
  return document.querySelector('.iget-reader-book') ||
         document.scrollingElement ||
         document.documentElement;
  };

  if (isWeRead || (isDedaoSite && !isDedaoReader)) {
  // 滚动监听（实时检测，节流 30ms 以降低频率但保持响应性）
  let scrollTimer = null;
  let lastScrollTop = 0;
  const onScroll = () => {
  if (scrollTimer) return;
  scrollTimer = setTimeout(() => {
      const el = getScrollEl();
      if (!el) {
          scrollTimer = null;
          return;
      }
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
  };

  const bindScrollListener = () => {
  const el = getScrollEl();
  if (!el) {
      setTimeout(bindScrollListener, 100);
      return;
  }
  el.addEventListener('scroll', onScroll, {passive:true});
  };
  bindScrollListener();
  }

  if (isDedaoReader) {
  let dedaoIdleTimer = null;
  let lastScrollTraceAt = 0;
  let lastMutationTraceAt = 0;
  let lastDedaoScrollTop = null;
  let lastClickScrollTop = null;
  let lastDedaoScrollQualified = false;
  let lastMutationAt = 0;
  let contentReadySent = false;
  let clickSeq = 0;
  let idleBaselineTop = null;
  let idleBaselineClickSeq = -1;
  const storedClickSeq = Number(window.__SMART_REFRESH_DEDAO_CLICK_SEQ__);
  if (Number.isFinite(storedClickSeq)) {
      clickSeq = storedClickSeq;
  } else {
      window.__SMART_REFRESH_DEDAO_CLICK_SEQ__ = 0;
  }
  const incClickSeq = () => {
      clickSeq = (Number.isFinite(clickSeq) ? clickSeq : 0) + 1;
      window.__SMART_REFRESH_DEDAO_CLICK_SEQ__ = clickSeq;
      return clickSeq;
  };
  const isDedaoScrollTarget = (target) => {
  if (!target) return false;
  if (target === document) return true;
  const root = document.scrollingElement || document.documentElement || document.body;
  if (target === root || target === document.documentElement || target === document.body) {
      return true;
  }
  if (target.nodeType === 1) {
      const el = target;
      if (el.classList && el.classList.contains('iget-reader-book')) return true;
      if (el.closest && el.closest('.iget-reader-book')) return true;
  }
  return false;
  };
  const updateClickBaseline = () => {
  const el = getScrollEl();
  if (el && typeof el.scrollTop === 'number') {
      lastClickScrollTop = el.scrollTop;
  }
  lastDedaoScrollQualified = false;
  if (dedaoIdleTimer) {
      clearTimeout(dedaoIdleTimer);
      dedaoIdleTimer = null;
  }
  incClickSeq();
  };
  window.__SMART_REFRESH_DEDAO_CLICK_SEQ_INC__ = incClickSeq;
  window.__SMART_REFRESH_DEDAO_CLICK_BASELINE__ = updateClickBaseline;
  document.addEventListener('touchend', updateClickBaseline,
                            {passive:true, capture:true});
  document.addEventListener('click', updateClickBaseline,
                            {passive:true, capture:true});

  const onDedaoScroll = (event) => {
  const target = event ? event.target : null;
  if (!isDedaoScrollTarget(target)) return;
  const now = Date.now();
  let tag = '';
  let cls = '';
  let top = 0;
  let scrollElTag = '';
  let scrollElId = '';
  let scrollElClass = '';
  let scrollElUid = '';
  let scrollElIsConnected = false;
  let scrollElScrollHeight = 0;
  let scrollElClientHeight = 0;
  let scrollElOffsetHeight = 0;
  let scrollElRectTop = 0;
  let scrollElRectHeight = 0;
  if (target && target.nodeType === 1) {
      tag = target.tagName || '';
      cls = target.className || '';
  }
  const scrollEl = getScrollEl();
  if (scrollEl && typeof scrollEl.scrollTop === 'number') {
      top = scrollEl.scrollTop;
      scrollElTag = scrollEl.tagName || '';
      scrollElId = scrollEl.id || '';
      scrollElClass = scrollEl.className || '';
      scrollElIsConnected = !!scrollEl.isConnected;
      if (!scrollEl.__smartRefreshDedaoUid) {
          scrollEl.__smartRefreshDedaoUid =
              'dedao-' + Math.random().toString(36).slice(2, 8);
      }
      scrollElUid = String(scrollEl.__smartRefreshDedaoUid || '');
      scrollElScrollHeight = scrollEl.scrollHeight || 0;
      scrollElClientHeight = scrollEl.clientHeight || 0;
      scrollElOffsetHeight = scrollEl.offsetHeight || 0;
      if (scrollEl.getBoundingClientRect) {
          const rect = scrollEl.getBoundingClientRect();
          if (rect) {
              scrollElRectTop = Math.round(rect.top || 0);
              scrollElRectHeight = Math.round(rect.height || 0);
          }
      }
  }
  let delta = 0;
  if (typeof top === 'number') {
      if (lastClickScrollTop !== null) {
          delta = Math.abs(top - lastClickScrollTop);
      } else if (lastDedaoScrollTop !== null) {
          delta = Math.abs(top - lastDedaoScrollTop);
      }
  }
  lastDedaoScrollTop = top;
  if (delta >= 786) {
      lastDedaoScrollQualified = true;
  }
  if (now - lastScrollTraceAt >= 200) {
      sendTrace('scroll_event', {targetTag: tag, targetClass: String(cls),
                                 scrollTop: top, delta: delta, clickSeq: clickSeq,
                                 eventTs: event ? event.timeStamp : 0,
                                 scrollElTag: scrollElTag,
                                 scrollElId: scrollElId,
                                 scrollElClass: String(scrollElClass),
                                 scrollElUid: scrollElUid,
                                 scrollElIsConnected: scrollElIsConnected,
                                 scrollElScrollHeight: scrollElScrollHeight,
                                 scrollElClientHeight: scrollElClientHeight,
                                 scrollElOffsetHeight: scrollElOffsetHeight,
                                 scrollElRectTop: scrollElRectTop,
                                 scrollElRectHeight: scrollElRectHeight});
      lastScrollTraceAt = now;
  }
  if (dedaoIdleTimer) clearTimeout(dedaoIdleTimer);
  idleBaselineTop = top;
  idleBaselineClickSeq = clickSeq;
  dedaoIdleTimer = setTimeout(() => {
      if (lastDedaoScrollQualified) {
          const el = getScrollEl();
          let currentTop = lastDedaoScrollTop;
          if (el && typeof el.scrollTop === 'number') {
              currentTop = el.scrollTop;
          }
          if (idleBaselineTop === currentTop &&
              idleBaselineClickSeq === clickSeq) {
              sendTrace('scroll_idle', {scrollTop: currentTop});
          }
      }
  }, 200);
  };
  document.addEventListener('scroll', onDedaoScroll, {passive:true, capture:true});

  const emitMutationTrace = (reason, target) => {
  const now = Date.now();
  if (now - lastMutationTraceAt < 200) return;
  let tag = '';
  let cls = '';
  if (target && target.nodeType === 1) {
      tag = target.tagName || '';
      cls = target.className || '';
  }
  const payload = {targetTag: tag, targetClass: String(cls)};
  if (reason === 'dom_mutation_book') {
      const el = getScrollEl();
      if (el && typeof el.scrollTop === 'number') {
          payload.scrollTop = el.scrollTop;
      }
      if (el && typeof el.scrollHeight === 'number') {
          payload.scrollHeight = el.scrollHeight;
      }
  }
  sendTrace(reason, payload);
  lastMutationTraceAt = now;
  lastMutationAt = now;
  };

  const bindMutationObserver = (selector, reason) => {
  const attach = () => {
      const el = document.querySelector(selector);
      if (!el) {
          setTimeout(attach, 200);
          return;
      }
      const observer = new MutationObserver(() => {
          emitMutationTrace(reason, el);
      });
      observer.observe(el, {childList:true, subtree:true, attributes:false, characterData:false});
  };
  attach();
  };

  bindMutationObserver('.iget-reader-book', 'dom_mutation_book');
  bindMutationObserver('.reader-book-wrapper', 'dom_mutation_wrapper');
  bindMutationObserver('.book-chapter-wrapper', 'dom_mutation_chapter');

  const checkContentReady = () => {
  if (contentReadySent) return;
  const title = String(document.title || '');
  const titleReady = title.length > 0 && title.indexOf('dedao.cn/ebook/reader') === -1;
  const bookEl = document.querySelector('.iget-reader-book');
  const bookVisible = !!(bookEl && bookEl.clientHeight > 0);
  const chapters = document.querySelectorAll('.book-chapter-wrapper');
  const hasChapters = chapters && chapters.length > 0;
  const stable = (Date.now() - lastMutationAt) >= 200;
  if (titleReady && bookVisible && hasChapters && stable) {
      sendTrace('content_ready', {title: title, chapters: chapters.length});
      contentReadySent = true;
  }
  };
  setInterval(checkContentReady, 200);
  }
  
  // 100ms 批量汇报（作为兜底，确保小事件也能被处理）
  // 书籍页面已禁用burstMode，所以移除!burstMode条件
  if (isWeRead || (isDedaoSite && !isDedaoReader)) {
  setInterval(() => {
  if (pendingEvents.length > 0) {
      console.log('[REFRESH_EVENTS]' + JSON.stringify(pendingEvents));
      pendingEvents = [];
      lastReportTime = Date.now();
  }
  }, 100);
  }
  
  })();
  )WR");
  QString targetExpr = QStringLiteral("true");
  if (flagName.contains(QStringLiteral("DEDAO_READER"), Qt::CaseInsensitive)) {
    targetExpr = QStringLiteral("isDedaoReader");
  } else if (flagName.contains(QStringLiteral("DEDAO_SITE"),
                                Qt::CaseInsensitive)) {
    targetExpr = QStringLiteral("isDedaoSite && !isDedaoReader");
  } else if (flagName.contains(QStringLiteral("DEDAO"),
                                Qt::CaseInsensitive)) {
    targetExpr = QStringLiteral("isDedaoSite");
  } else if (flagName.contains(QStringLiteral("WR"), Qt::CaseInsensitive)) {
    targetExpr = QStringLiteral("isWeRead");
  }
  return js.arg(flagName, targetExpr);
}

void WereadBrowser::installWeReadFontClampScript() {
  installWeReadDefaultSettingsScript();
}

void WereadBrowser::logScriptInventory(bool once) {
  if (once && m_scriptInventoryLogged)
    return;
  if (!m_view || !m_view->page())
    return;
  if (!isWeReadBook() && !isDedaoBook())
    return;
  if (once)
    m_scriptInventoryLogged = true;
  const QString js = QStringLiteral(
      "(() => {"
      "  try {"
      "    const list = Array.from(document.scripts || []);"
      "    const mapped = list.map((s, idx) => ({idx, src: s.src || '', "
      "inlineLen: (s.innerText || '').length}));"
      "    console.warn('[SCRIPT_LIST]', JSON.stringify(mapped));"
      "    return mapped;"
      "  } catch(e) { return {error:String(e)}; }"
      "})()");
  m_view->page()->runJavaScript(
      js, [](const QVariant &res) { qInfo() << "[SCRIPT_LIST]" << res; });
}

void WereadBrowser::logResourceEntries(bool once) {
  if (once && m_resourceLogged)
    return;
  if (!m_view || !m_view->page())
    return;
  if (!isWeReadBook() && !isDedaoBook())
    return;
  if (once)
    m_resourceLogged = true;
  const QString js = QStringLiteral(
      "(() => {"
      "  try {"
      "    const entries = performance.getEntriesByType('resource') || [];"
      "    const filtered = entries.filter(e => "
      "/app\\.|wrweb|wrwebnjlogic/i.test(e.name || ''));"
      "    const mapped = filtered.map(e => ({"
      "      name: e.name, type: e.initiatorType, transferSize: "
      "e.transferSize, encodedSize: e.encodedBodySize, decodedSize: "
      "e.decodedBodySize, dur: e.duration, respEnd: e.responseEnd"
      "    }));"
      "    const core = entries.filter(e => "
      "/wrwebnjlogic\\/js\\/35\\.43e6aa07\\.js/i.test(e.name || ''));"
      "    const coreMapped = core.map(e => ({"
      "      name: e.name, type: e.initiatorType, transferSize: "
      "e.transferSize, encodedSize: e.encodedBodySize, decodedSize: "
      "e.decodedBodySize, dur: e.duration, respEnd: e.responseEnd"
      "    }));"
      "    console.warn('[RES_ENTRY]', JSON.stringify(mapped));"
      "    console.warn('[RES_ENTRY_CORE]', JSON.stringify(coreMapped));"
      "    return mapped;"
      "  } catch(e) { return {error:String(e)}; }"
      "})()");
  m_view->page()->runJavaScript(
      js, [](const QVariant &res) { qInfo() << "[RES_ENTRY]" << res; });
}

void WereadBrowser::logComputedFontOnce() {
  if (m_fontLogged)
    return;
  if (!m_view || !m_view->page())
    return;
  if (!isWeReadBook() && !isDedaoBook())
    return;
  const bool isDedao = isDedaoBook();
  m_fontLogged = true;
  const QString js =
      isDedao ? QStringLiteral(
                    "(() => {"
                    "  try {"
                    "    const cont = "
                    "document.querySelector('.iget-reader-book') || "
                    "document.querySelector('.reader-content') || "
                    "document.querySelector('.iget-reader-container') || "
                    "document.querySelector('.reader-body') || "
                    "document.body;"
                    "    const style = cont ? getComputedStyle(cont) : null;"
                    "    let setting = {};"
                    "    let raw = '';"
                    "    try { raw = localStorage.getItem('readerSettings') || "
                    "''; } catch(e) { raw = ''; }"
                    "    if (raw) {"
                    "      try {"
                    "        const parsed = JSON.parse(raw);"
                    "        if (parsed && typeof parsed === 'object') "
                    "setting = parsed;"
                    "      } catch(e) { setting = {}; }"
                    "    }"
                    "    return {"
                    "      site: 'dedao',"
                    "      fontFamily: style ? style.fontFamily : '',"
                    "      fontSize: style ? style.fontSize : '',"
                    "      fontWeight: style ? style.fontWeight : '',"
                    "      settingFont: setting.fontFamily || '',"
                    "      settingLevel: setting.fontLevel || '',"
                    "      settingRaw: raw || ''"
                    "    };"
                    "  } catch(e) { return {error:String(e), site:'dedao'}; }"
                    "})()")
              : QStringLiteral(
                    "(() => {"
                    "  try {"
                    "    const cont = "
                    "document.querySelector('.renderTargetContent') || "
                    "document.body;"
                    "    const style = cont ? getComputedStyle(cont) : null;"
                    "    let setting = {};"
                    "    try { setting = "
                    "JSON.parse(localStorage.getItem('wrLocalSetting') "
                    "|| '{}'); } catch(e) { setting = {}; }"
                    "    return {"
                    "      site: 'weread',"
                    "      fontFamily: style ? style.fontFamily : '',"
                    "      fontSize: style ? style.fontSize : '',"
                    "      fontWeight: style ? style.fontWeight : '',"
                    "      settingFont: setting.fontFamily || '',"
                    "      settingSize: setting.fontSizeLevel || ''"
                    "    };"
                    "  } catch(e) { return {error:String(e), site:'weread'}; }"
                    "})()");
  m_view->page()->runJavaScript(
      js, [](const QVariant &res) { qInfo() << "[FONT] computed" << res; });
}

void WereadBrowser::logTextScan(bool once) {
  if (once && m_textScanDone)
    return;
  if (!m_view || !m_view->page())
    return;
  if (!isWeReadBook() && !isDedaoBook())
    return;
  if (once)
    m_textScanDone = true;
  const QString js = QStringLiteral(
      "(() => {"
      "  try {"
      "    const bodyLen = (document.body && document.body.innerText) ? "
      "document.body.innerText.length : 0;"
      "    let shadowText = 0;"
      "    const all = Array.from(document.querySelectorAll('*'));"
      "    let canvasCount = 0, imgCount = 0;"
      "    all.forEach(el => {"
      "      const tag = (el.tagName || '').toLowerCase();"
      "      if (tag === 'canvas') canvasCount++;"
      "      if (tag === 'img') imgCount++;"
      "      if (el.shadowRoot && el.shadowRoot.innerText) shadowText += "
      "el.shadowRoot.innerText.length;"
      "    });"
      "    let iframeText = 0; let iframeBlocked = 0; let iframeCount = 0;"
      "    Array.from(document.querySelectorAll('iframe')).forEach(f => {"
      "      iframeCount++;"
      "      try {"
      "        const d = f.contentDocument || (f.contentWindow && "
      "f.contentWindow.document);"
      "        if (d && d.body && d.body.innerText) iframeText += "
      "d.body.innerText.length;"
      "      } catch(_) { iframeBlocked++; }"
      "    });"
      "    // 也统计 renderTarget/readerChapter 节点长度，方便对比"
      "    const core = document.querySelector('.renderTargetContent') || "
      "document.querySelector('.readerChapterContent');"
      "    const coreLen = core ? ((core.innerText||'').length) : 0;"
      "    return {bodyLen, shadowText, iframeText, iframeCount, "
      "iframeBlocked, canvasCount, imgCount};"
      "  } catch(e) { return {error:String(e)}; }"
      "})()");
  m_view->page()->runJavaScript(js, [](const QVariant &res) {
    qWarning().noquote() << "[TEXT_SCAN]" << res;
  });
}

void WereadBrowser::weReadScroll(bool forward) {
  if (!m_view || !m_view->page()) {
    m_navigating = false;
    return;
  }
  const int currentSeq = ++m_navSequence;
  m_navigating = true;
  const int stepPx =
      qRound((m_view ? m_view->height() : 1600) * (forward ? 0.8 : -0.8));
  const qint64 cppSend = QDateTime::currentMSecsSinceEpoch();
  qInfo() << "[PAGER] weRead scroll dispatch"
          << "forward" << forward << "step" << stepPx << "seq" << currentSeq
          << "ts" << cppSend;

  QTimer::singleShot(1200, this, [this, currentSeq]() {
    if (m_navigating && m_navSequence == currentSeq) {
      m_navigating = false;
      qWarning() << "[PAGER] weRead force unlock (timeout 1200ms) seq"
                 << currentSeq;
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
            )")
                         .arg(stepPx);

  m_view->page()->runJavaScript(
      js, [this, cppSend, currentSeq, forward](const QVariant &res) {
        const qint64 cppRecv = QDateTime::currentMSecsSinceEpoch();
        const QVariantMap m = res.toMap();
        const qint64 jsStart =
            m.value(QStringLiteral("t_js_start")).toLongLong();
        const qint64 jsEnd = m.value(QStringLiteral("t_js_end")).toLongLong();
        const qint64 total = cppRecv - cppSend;
        const qint64 queue = jsStart ? (jsStart - cppSend) : -1;
        const qint64 exec = (jsEnd && jsStart) ? (jsEnd - jsStart) : -1;
        const qint64 ipc = (jsEnd) ? (cppRecv - jsEnd) : -1;
        const QString tag = m.value(QStringLiteral("tag")).toString();
        const int delta = m.value(QStringLiteral("delta")).toInt();

        if (total > 800) {
          qWarning().noquote()
              << QString(
                     "[PAGER] weRead SLOW | Total:%1ms Queue:%2ms Exec:%3ms "
                     "IPC:%4ms Tag:%5 Delta:%6")
                     .arg(total)
                     .arg(queue)
                     .arg(exec)
                     .arg(ipc)
                     .arg(tag)
                     .arg(delta);
        } else {
          qInfo().noquote() << QString("[PAGER] weRead OK   | Total:%1ms "
                                       "Queue:%2ms Exec:%3ms Tag:%4 Delta:%5")
                                   .arg(total)
                                   .arg(queue)
                                   .arg(exec)
                                   .arg(tag)
                                   .arg(delta);
        }
        if (isWeReadBook() && delta == 0) {
          qWarning().noquote() << QString("[PAGER] weRead scroll delta=0, "
                                          "fallback to pager button (%1)")
                                      .arg(forward ? QStringLiteral("next")
                                                   : QStringLiteral("prev"));
          const QString clickJs =
              forward
                  ? QStringLiteral(
                        "(() => {"
                        "  const btn = "
                        "document.querySelector('.renderTarget_pager_button_"
                        "right');"
                        "  if (!btn) return {ok:false, reason:'no-next-btn'};"
                        "  const span = btn.querySelector('span') || btn;"
                        "  "
                        "['pointerdown','pointerup','click'].forEach(t=>span."
                        "dispatchEvent(new "
                        "PointerEvent(t,{bubbles:true,cancelable:true})));"
                        "  return {ok:true, action:'next'};"
                        "})()")
                  : QStringLiteral(
                        "(() => {"
                        "  const btn = "
                        "document.querySelector('.renderTarget_pager_button');"
                        "  if (!btn) return {ok:false, reason:'no-prev-btn'};"
                        "  const span = btn.querySelector('span') || btn;"
                        "  "
                        "['pointerdown','pointerup','click'].forEach(t=>span."
                        "dispatchEvent(new "
                        "PointerEvent(t,{bubbles:true,cancelable:true})));"
                        "  return {ok:true, action:'prev'};"
                        "})()");
          m_view->page()->runJavaScript(clickJs, [forward](const QVariant &r) {
            qInfo() << "[PAGER] weRead pager fallback result"
                    << (forward ? "next" : "prev") << r;
          });
        }
        if (m_navSequence == currentSeq) {
          m_navigating = false;
        }
      });
}

void WereadBrowser::triggerStalledRescue(const QString &reason, int chapterLen,
                                         int bodyLen, int pollCount) {
  if (!m_view || !m_view->page() || !isWeReadBook())
    return;
  m_stallRescueTriggered = true;
  const int seq = ++m_rescueSeq;
  const QString js = QStringLiteral(
      "(() => {"
      "  const btn = "
      "document.querySelector('.renderTarget_pager_button_right');"
      "  if (btn && btn.click) { btn.click(); return {ok:true, "
      "action:'click-next'}; }"
      "  location.reload();"
      "  return {ok:true, action:'reload'};"
      "})()");
  m_view->page()->runJavaScript(js, [this, seq, reason, chapterLen, bodyLen,
                                     pollCount](const QVariant &res) {
    qWarning() << "[RESCUE] stalled seq" << seq << "reason" << reason
               << "pollCount" << pollCount << "chapterLen" << chapterLen
               << "bodyLen" << bodyLen << res;
  });
}

// 会话恢复方法实现（类外实现）

void WereadBrowser::openDedaoMenu() {

  if (!isDedaoBook() || !m_view || !m_view->page())

    return;

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

  m_view->page()->runJavaScript(

      js, [](const QVariant &res) { qInfo() << "[MENU] dedao open" << res; });
}

QString WereadBrowser::getSessionStatePath() {

  QString dataDir =

      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

  return dataDir + "/session_state.json";
}

QString WereadBrowser::getExitReasonPath() {

  QString dataDir =

      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

  return dataDir + "/.exit_reason";
}

void WereadBrowser::saveExitReason(const QString &reason) {

  QString filePath = getExitReasonPath();

  QFile file(filePath);

  if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {

    QTextStream out(&file);

    out << reason;

    file.close();

    qInfo() << "[EXIT] Saved exit reason:" << reason;

  } else {

    qWarning() << "[EXIT] Failed to save exit reason:" << filePath;
  }
}

QString WereadBrowser::loadExitReason() {

  QString filePath = getExitReasonPath();

  QFile file(filePath);

  if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {

    QTextStream in(&file);

    QString reason = in.readAll().trimmed();

    file.close();

    // 读取后立即删除标记文件，避免影响下次启动

    file.remove();

    qInfo() << "[EXIT] Loaded exit reason:" << reason;

    return reason;
  }

  // 如果文件不存在，返回 "unknown"（默认不恢复会话）

  qInfo() << "[EXIT] No exit reason file found, defaulting to 'unknown'";

  return QStringLiteral("unknown");
}

namespace {
bool isErrorUrlString(const QString &candidate) {
  if (candidate.isEmpty())
    return false;
  if (candidate.startsWith(QStringLiteral("chrome-error://")))
    return true;
  const QUrl parsed = QUrl::fromUserInput(candidate);
  return parsed.scheme() == QLatin1String("chrome-error");
}

bool isBookUrlString(const QString &candidate) {
  if (candidate.isEmpty())
    return false;
  const bool isWeRead = candidate.contains(QStringLiteral("weread.qq.com")) &&
                        candidate.contains(QStringLiteral("/web/reader"));
  const bool isDedao = candidate.contains(QStringLiteral("dedao.cn")) &&
                       candidate.contains(QStringLiteral("/ebook/reader"));
  return isWeRead || isDedao;
}

QJsonObject readSessionStateFile(const QString &filePath) {
  QFile file(filePath);
  if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
    return QJsonObject();
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  file.close();
  return doc.object();
}

QString pickLastGoodUrl(const QJsonObject &existing) {
  const QString recordedGood = existing.value("lastGoodUrl").toString();
  if (!recordedGood.isEmpty() && !isErrorUrlString(recordedGood))
    return recordedGood;
  const QString recordedUrl = existing.value("url").toString();
  if (!recordedUrl.isEmpty() && !isErrorUrlString(recordedUrl))
    return recordedUrl;
  return QString();
}

QJsonObject buildSessionState(const QString &urlStr, int scrollY, qint64 now,
                              const QJsonObject &existing) {
  QJsonObject sessionState;
  sessionState["url"] = urlStr;
  sessionState["lastGoodUrl"] = urlStr;
  sessionState["scrollY"] = scrollY;
  sessionState["timestamp"] = now;
  if (isBookUrlString(urlStr)) {
    sessionState["lastBookUrl"] = urlStr;
    sessionState["lastBookScrollY"] = scrollY;
    return sessionState;
  }
  const QString lastBookUrl = existing.value("lastBookUrl").toString();
  if (!lastBookUrl.isEmpty())
    sessionState["lastBookUrl"] = lastBookUrl;
  if (existing.contains("lastBookScrollY"))
    sessionState["lastBookScrollY"] =
        existing.value("lastBookScrollY").toInt();
  return sessionState;
}

bool writeSessionStateFile(const QString &filePath,
                           const QJsonObject &sessionState) {
  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    return false;
  const QJsonDocument doc(sessionState);
  file.write(doc.toJson());
  file.close();
  return true;
}
} // namespace

void WereadBrowser::saveSessionState() const {

  if (!m_view || !m_view->page())
    return;

  const QUrl url = m_view->url();
  if (url.isEmpty() || url.scheme() == "about")
    return;

  // 防抖：如果 10 秒内已保存，跳过本次保存（避免频繁写入）
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (m_lastSessionSaveTime > 0 && (now - m_lastSessionSaveTime) < 10000) {
    qInfo() << "[SESSION] Skip save (too soon, last save was"
            << (now - m_lastSessionSaveTime) << "ms ago)";
    return;
  }

  // 通过 JavaScript 获取滚动位置
  m_view->page()->runJavaScript(
      QStringLiteral("({url: window.location.href, scrollY: window.scrollY})"),
      [this, now](const QVariant &result) {
        const QJsonObject obj = result.toJsonObject();
        const QString urlStr = obj.value("url").toString();
        const int scrollY = obj.value("scrollY").toInt();
        const QString filePath = WereadBrowser::getSessionStatePath();
        const QJsonObject existing = readSessionStateFile(filePath);

        if (isErrorUrlString(urlStr)) {
          const QString lastGoodUrl = pickLastGoodUrl(existing);
          if (!lastGoodUrl.isEmpty()) {
            qInfo() << "[SESSION] Skip save (error url), keep last good:"
                    << lastGoodUrl;
          } else {
            qWarning() << "[SESSION] Skip save (error url, no last good):"
                       << urlStr;
          }
          return;
        }

        const QJsonObject sessionState =
            buildSessionState(urlStr, scrollY, now, existing);
        if (writeSessionStateFile(filePath, sessionState)) {
          // 更新保存时间戳（mutable 允许在 const 方法中修改）
          m_lastSessionSaveTime = now;
          qInfo() << "[SESSION] Saved session state:" << urlStr
                  << "scrollY:" << scrollY;
        }
      });
}
