#include "weread_browser.h"
#include <QTextStream>

void WereadBrowser::dedaoScroll(bool forward) {
    if (!m_view || !m_view->page()) {
      m_navigating = false;
      return;
    }

    const int currentSeq = ++m_navSequence;
    m_navigating = true;

    // 看门狗保持 1200ms
    QTimer::singleShot(1200, this, [this, currentSeq]() {
      if (m_navigating && m_navSequence == currentSeq) {
        m_navigating = false;
        qWarning() << "[PAGER] Force unlock (timeout 1200ms) seq" << currentSeq;
      }
    });

    const int step = forward ? 1000 : -1000;
    // 【打点1】记录 C++ 发出指令的时间
    const qint64 cppSendTime = QDateTime::currentMSecsSinceEpoch();
    qInfo() << "[PAGER] dedao dispatch seq" << currentSeq << "forward"
            << forward << "ts" << cppSendTime;

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
            )")
                           .arg(step);

    m_view->page()->runJavaScript(js, [this, cppSendTime,
                                       currentSeq](const QVariant &res) {
      // 【打点4】记录 C++ 收到回调的时间
      const qint64 cppRecvTime = QDateTime::currentMSecsSinceEpoch();

      const QVariantMap m = res.toMap();
      const qint64 jsStartTime = m.value("t_js_start").toLongLong();
      const qint64 jsEndTime = m.value("t_js_end").toLongLong();

      const qint64 queueDelay = jsStartTime - cppSendTime; // 排队耗时
      const qint64 execTime = jsEndTime - jsStartTime;     // JS执行耗时
      const qint64 ipcDelay = cppRecvTime - jsEndTime;     // 回传耗时
      const qint64 totalElapsed = cppRecvTime - cppSendTime;

      const int delta = m.value("delta").toInt();
      const QString mode = m.value("mode").toString();

      if (totalElapsed > 800) {
        qWarning().noquote()
            << QString("[PAGER] SLOW | Total: %1ms | Queue: %2ms | Exec: %3ms "
                       "| IPC: %4ms | Mode: %5 | Delta: %6")
                   .arg(totalElapsed)
                   .arg(queueDelay)
                   .arg(execTime)
                   .arg(ipcDelay)
                   .arg(mode)
                   .arg(delta);
      } else {
        qInfo().noquote() << QString("[PAGER] OK   | Total: %1ms | Queue: %2ms "
                                     "| Exec: %3ms | Mode: %4 | Delta: %5")
                                 .arg(totalElapsed)
                                 .arg(queueDelay)
                                 .arg(execTime)
                                 .arg(mode)
                                 .arg(delta);
      }

      if (m_navSequence == currentSeq) {
        m_navigating = false;
      } else {
        qInfo() << "[PAGER] dedao callback ignored, seq mismatch";
      }
    });
  }


void WereadBrowser::fetchCatalog() {
    if (!m_view || !m_view->page())
      return;
    qInfo() << "[CATALOG] Fetching starting...";

    // Step 1: Click catalog button (synchronous IIFE)
    const QString clickJs = QStringLiteral(R"(
(() => {
  const list = document.querySelector('.readerCatalog_list');
  const panel = document.querySelector('.readerCatalog');
  const panelVisible = panel ? (getComputedStyle(panel).display !== 'none') : false;
  if (list && list.children.length > 0) {
    return {ok:true, action:'already-open', count: list.children.length, panelVisible};
  }
  const selectors = [
    '.readerControls_item.catalog',
    '.readerControls .catalog',
    'button[title*="\u76ee\u5f55"]',
    '[data-action="catalog"]'
  ];
  const isVisible = (el) => {
    if (!el) return false;
    const style = window.getComputedStyle ? getComputedStyle(el) : null;
    if (style) {
      if (style.display === 'none' || style.visibility === 'hidden' || style.opacity === '0') {
        return false;
      }
      if (style.pointerEvents === 'none') return false;
    }
    const rect = el.getBoundingClientRect ? el.getBoundingClientRect() : null;
    if (rect && (rect.width === 0 || rect.height === 0)) return false;
    return true;
  };
  const found = [];
  let picked = null;
  for (const sel of selectors) {
    const el = document.querySelector(sel);
    if (el) {
      const info = {sel, tag: el.tagName, cls: el.className || '', visible: isVisible(el)};
      found.push(info);
      if (!picked) picked = {el, sel, visible: info.visible};
    }
  }
  if (picked && picked.el) {
    if (picked.el.click) picked.el.click();
    return {
      ok:true,
      action:'clicked',
      selector: picked.sel,
      visible: picked.visible,
      panelVisible,
      found
    };
  }
  return {ok:false, error:'no-btn', panelVisible, found};
})()
    )");

    m_view->page()->runJavaScript(clickJs, [this](const QVariant &res) {
      qInfo() << "[CATALOG] Click result:" << res;
      QVariantMap resMap = res.toMap();
      if (!resMap.value("ok", false).toBool()) {
        qWarning() << "[CATALOG] Failed to open catalog:" << resMap;
        return;
      }
    });

    // Step 2: After delay, scrape catalog data (synchronous IIFE)
    QTimer::singleShot(500, this, [this]() {
      const QString scrapeJs = QStringLiteral(R"(
(() => {
  console.log('[CATALOG_DEBUG] Starting catalog scrape (dom-only)...');
  const items = Array.from(document.querySelectorAll('.readerCatalog_list_item'));
  console.log('[CATALOG_DEBUG] Found ' + items.length + ' DOM items');
  if (items.length === 0) {
    return {ok:false, error:'no-items'};
  }
  const normalize = (s) => String(s || '').replace(/\s+/g, ' ').trim();
  const data = items.map((item, idx) => {
    const titleEl = item.querySelector('.readerCatalog_list_item_title_text');
    let title = titleEl ? titleEl.innerText : '';
    if (!title) {
      title = item.innerText || '';
    }
    title = normalize(title);
    const isCurrent = item.classList.contains('readerCatalog_list_item_selected');
    let level = 1;
    const inner = item.querySelector('.readerCatalog_list_item_inner');
    const className = inner ? inner.className : item.className;
    const m = String(className || '').match(/readerCatalog_list_item_level_(\d+)/);
    if (m) {
      const parsed = parseInt(m[1], 10);
      level = Number.isFinite(parsed) && parsed > 0 ? parsed : 1;
    }
    return {
      index: idx,
      title: title,
      level: level,
      isCurrent: isCurrent,
      matchKey: title,
      chapterUid: title
    };
  }).filter((item) => item && item.title);
  return {ok:true, chapters: data, source:'dom-only'};
})()
      )");

      m_view->page()->runJavaScript(scrapeJs, [this](const QVariant &v) {
        qInfo() << "[CATALOG] Scrape result type:" << v.typeName();
        QVariantMap resMap = v.toMap();
        if (!resMap.value("ok", false).toBool()) {
          qWarning() << "[CATALOG] Scrape failed:" << resMap;
          return;
        }

        QVariantList chapterList = resMap.value("chapters").toList();
        qInfo() << "[CATALOG] Got" << chapterList.size() << "chapters";

        QJsonArray chapters;
        for (const auto &item : chapterList) {
          chapters.append(QJsonObject::fromVariantMap(item.toMap()));
        }

        if (m_catalogWidget) {
          m_catalogWidget->loadChapters(chapters);
          qInfo() << "[CATALOG] Widget shown, visible:"
                  << m_catalogWidget->isVisible();
        } else {
          qWarning() << "[CATALOG] m_catalogWidget is null!";
        }
      });
    });
  }


void WereadBrowser::goBack() {
    if (!m_view || !m_view->page())
      return;
    auto *hist = m_view->page()->history();
    const bool canBack = hist && hist->canGoBack();
    const int count = hist ? hist->count() : -1;
    const int idx = hist ? hist->currentItemIndex() : -1;
    const QString currentUrl = m_view->url().toString();
    const bool inReader = currentUrl.contains(QStringLiteral("/web/reader/"));
    const bool isDedaoReader = isDedaoBook();

    qInfo() << "[BACK] canGoBack" << canBack << "count" << count
            << "currentIndex" << idx << "url" << m_view->url();

    // Dedao 特例：直接跳转详情页，避免空 history.back
    if (isDedaoReader) {
      const QString cur = m_view->url().toString();
      QString detail = cur;
      const int pos = cur.indexOf(QStringLiteral("/ebook/reader"));
      if (pos >= 0) {
        detail = cur;
        detail.replace(QStringLiteral("/ebook/reader"),
                       QStringLiteral("/ebook/detail"));
      }
      qInfo() << "[BACK] dedao redirect to detail" << detail;
      m_allowDedaoDetailOnce = true; // 允许一次详情跳转
      m_view->setUrl(QUrl(detail));
      return;
    }

    if (canBack) {
      // Use JS history.back so SPA can handle its own stack; then log the
      // result a moment later.
      m_view->page()->runJavaScript(QStringLiteral("window.history.back();"));
      QTimer::singleShot(800, this, [this]() {
        auto *h = m_view->page()->history();
        const int afterCount = h ? h->count() : -1;
        const int afterIdx = h ? h->currentItemIndex() : -1;
        qInfo() << "[BACK] after-back url" << m_view->url() << "count"
                << afterCount << "currentIndex" << afterIdx;
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


void WereadBrowser::goWeReadHome() {
    if (!m_view)
      return;
    const QUrl home(QStringLiteral("https://weread.qq.com/"));
    qInfo() << "[NAV] go weread home" << home;
    m_view->setUrl(home);
  }


void WereadBrowser::allowDedaoDetailOnce() { m_allowDedaoDetailOnce = true; }



void WereadBrowser::showMenu() {
    if (!m_menu)
      return;
    // stretch to top with margin to ensure visibility
    const int w =
        m_view ? m_view->width() : (this->width() > 0 ? this->width() : 954);
    const int x = 10;
    const int y = 70; // further down by 10px
    const int h = 100;
    const int menuWidth = qMax(100, w - 2 * x); // full width, no right reserve
    m_menu->setGeometry(x, y, menuWidth, h);
    m_menu->setVisible(true);
    m_menu->raise(); // 确保菜单在最上层
    m_menu->setAttribute(Qt::WA_TransparentForMouseEvents,
                         false); // 确保菜单能接收鼠标事件
    m_menu->setAttribute(Qt::WA_AcceptTouchEvents,
                         true); // 确保菜单能接收触摸事件
    m_menu->activateWindow();   // 激活窗口以确保焦点
    qInfo() << "[MENU] show overlay";
    m_menuTimer.start();
    // 通知智能刷新管理器菜单显示
    if (m_smartRefresh) {
      m_smartRefresh->triggerMenu();
    }
    QTimer::singleShot(5000, this, [this]() { captureFrame(); });
    QTimer::singleShot(6000, this, [this]() { captureFrame(); });
    runDomDiagnostic();
  }


bool WereadBrowser::handleMenuTap(const QPointF &globalPos) {
    if (!m_menu || !m_menu->isVisible())
      return false;
    const QPoint localPos = m_menu->mapFromGlobal(globalPos.toPoint());
    if (!m_menu->rect().contains(localPos))
      return false;
    QWidget *target = m_menu->childAt(localPos);
    QWidget *check = target;
    while (check && !qobject_cast<QPushButton *>(check)) {
      check = check->parentWidget();
    }
    auto *button = qobject_cast<QPushButton *>(check);
    if (button) {
      qInfo() << "[MENU] fallback click" << button->text() << "pos"
              << localPos;
      button->click();
      return true;
    }
    qInfo() << "[MENU] fallback tap in overlay (no button)" << "pos"
            << localPos;
    return true;
  }


void WereadBrowser::exitToXochitl(const QString &exitReason ) {
    // 保存退出方式标记
    saveExitReason(exitReason);
    // 先保存会话状态
    saveSessionState();
    // 启动退出脚本（停止后端，启动 xochitl）
    QProcess::startDetached(
        QStringLiteral("/home/root/weread/exit-wechatread.sh"));
    // 延迟退出，确保会话状态保存完成（saveSessionState 是异步的）
    QTimer::singleShot(500, this, []() { QApplication::quit(); });
  }


void WereadBrowser::triggerFullRefresh() {
    if (m_fbRef) {
      const int w = width();
      const int h = height();
      if (!m_blackOverlay) {
        m_blackOverlay = new QFrame(this);
        m_blackOverlay->setStyleSheet(QStringLiteral("background-color:#000;"));
        m_blackOverlay->setAutoFillBackground(true);
        m_blackOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_blackOverlay->hide();
      }
      m_blackOverlay->setGeometry(0, 0, w, h);
      m_blackOverlay->show();
      m_blackOverlay->raise();
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

      qInfo()
          << "[EINK] Manual full refresh triggered (BLACK -> INIT + GC16 FULL)";
      m_fbRef->refreshCleanup(w, h);
      static constexpr int kFullRefreshDelayMs = 150;
      QTimer::singleShot(kFullRefreshDelayMs, this, [this, w, h]() {
        if (!m_fbRef) {
          if (m_blackOverlay) {
            m_blackOverlay->hide();
          }
          qWarning()
              << "[EINK] Cannot run follow-up full refresh: m_fbRef is null";
          return;
        }
        if (m_blackOverlay) {
          m_blackOverlay->hide();
          QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        qInfo() << "[EINK] Manual full refresh follow-up (GC16 FULL)";
        m_fbRef->refreshFull(w, h);
      });
    } else {
      qWarning() << "[EINK] Cannot trigger full refresh: m_fbRef is null";
    }
  }


void WereadBrowser::sendBookState() {
    const bool isBook = isWeReadBook() || isDedaoBook();
    QByteArray b(1, isBook ? '\x01' : '\x00');
    m_stateSender.writeDatagram(b, QHostAddress(QStringLiteral("127.0.0.1")),
                                45456);
    qInfo() << "[STATE] isBookPage" << isBook << "url" << currentUrl;
    const bool wasBook = m_prevIsBookPage;
    m_prevIsBookPage = isBook;
    m_isBookPage = isBook;
    // 非书籍页：交互优先；书籍页：阅读优先
    if (m_smartRefresh) {
      m_smartRefresh->setBookPage(isBook);
      const auto targetPolicy =
          isBook ? SmartRefreshManager::PolicyReadingFirst
                 : SmartRefreshManager::PolicyInteractionFirst;
      if (m_smartRefresh->policy() != targetPolicy) {
        m_smartRefresh->setPolicy(targetPolicy);
      }
    }
    // 动态调整缩放：非书籍页保持 2.0，书籍页保持 1.5
    if (m_view) {
      const qreal targetZoom = isBook ? 1.5 : 2.0;
      if (!qFuzzyCompare(m_view->zoomFactor(), targetZoom)) {
        m_view->setZoomFactor(targetZoom);
        qInfo() << "[ZOOM] set zoom" << targetZoom << "isBook" << isBook;
      }
    }
    if (isBook && !wasBook) {
      cancelPendingCaptures(); // 取消之前页面的待执行抓帧
      m_firstFrameDone = false;
      m_reloadAttempts = 0;
      m_highWhiteRescueDone = false;
      m_dedaoProbed = false;
      m_emptyReadyPolls = 0;
      m_stallRescueTriggered = false;
      m_diagRan = false;
      m_contentReadyTriggered = false;
      m_scriptInventoryLogged = false;
      m_resourceLogged = false;
      m_textScanDone = false;
      m_rescueSeq = 0;
      m_iframeFixApplied = false;
      m_lowPowerCssInjected = false;
      m_fontLogged = false;
    }
    if (!isBook)
      m_scriptInventoryLogged = false;
    updateHeartbeat();
    if (isBook && !wasBook) {
    }
  }


void WereadBrowser::handleStateRequest() {
    while (m_stateResponder.hasPendingDatagrams()) {
      QByteArray d;
      d.resize(int(m_stateResponder.pendingDatagramSize()));
      QHostAddress sender;
      quint16 port = 0;
      m_stateResponder.readDatagram(d.data(), d.size(), &sender, &port);
      Q_UNUSED(d);
      sendBookState(); // reply by sending current state to 45456 as usual
      qInfo() << "[STATE] request received from" << sender.toString() << "port"
              << port;
    }
  }


QString WereadBrowser::detectUaMode(const QString &ua) const {
    const QString low = ua.toLower();
    if (low.contains(QStringLiteral("ipad")))
      return QStringLiteral("ipad");
    if (low.contains(QStringLiteral("kindle")))
      return QStringLiteral("kindle");
    if (low.contains(QStringLiteral("android")))
      return QStringLiteral("android");
    return QStringLiteral("default");
  }


bool WereadBrowser::isWeReadBook() const {
    const QString u = currentUrl.toString();
    // 放宽匹配：只要域是 weread.qq.com 且路径包含 /web/reader 即视为书籍页
    return u.contains(QStringLiteral("weread.qq.com")) &&
           u.contains(QStringLiteral("/web/reader"));
  }


bool WereadBrowser::isKindleUA() const {
    const QString ua = m_currentUA.toLower();
    return ua.contains(QStringLiteral("kindle")) ||
           m_uaMode.compare(QStringLiteral("kindle"), Qt::CaseInsensitive) == 0;
  }


bool WereadBrowser::isWeReadKindleMode() const { return isWeReadBook() && isKindleUA(); }



bool WereadBrowser::isDedaoBook() const {
    const QString u = currentUrl.toString();
    return u.contains(QStringLiteral("dedao.cn")) &&
           u.contains(QStringLiteral("/ebook/reader"));
  }


QWebEngineView *WereadBrowser::getView() const { return m_view; }



SmartRefreshManager *WereadBrowser::getSmartRefresh() const { return m_smartRefresh; }



void WereadBrowser::scheduleBookCaptures(bool force ) {
    Q_UNUSED(force);
    // 一体化模式不抓帧，仅保留接口
  }


void WereadBrowser::restartCaptureLoop() { /* no-op */
  }


void WereadBrowser::updateLastTouchTs(qint64 ts) { m_lastTouchTs = ts; }



void WereadBrowser::forceRepaintNudge() {
    if (!m_view || !m_view->page())
      return;
    const QString js =
        QStringLiteral("(() => {"
                       "  try {"
                       "    window.scrollBy(0,1);"
                       "    setTimeout(()=>window.scrollBy(0,-1),50);"
                       "    const b=document.body; if(b){ const "
                       "op=b.style.opacity; b.style.opacity='0.999'; "
                       "setTimeout(()=>{b.style.opacity=op||'';},50); }"
                       "    return {ok:true};"
                       "  } catch(e) { return {ok:false, error:String(e)}; }"
                       "})();");
    m_view->page()->runJavaScript(
        js, [](const QVariant &res) { qInfo() << "[REPAINT_NUDGE]" << res; });
  }


void WereadBrowser::logPageTurnEffect(int currentSeq, const QString &source, const QString &trigger, int score) {
    if (m_lastPageTurnEffectSeq == currentSeq)
      return;
    m_lastPageTurnEffectSeq = currentSeq;
    if (!trigger.isEmpty() && score >= 0) {
      qInfo() << "[PAGER_EFFECT]" << source << "seq" << currentSeq << "trigger"
              << trigger << "score" << score;
    } else if (!trigger.isEmpty()) {
      qInfo() << "[PAGER_EFFECT]" << source << "seq" << currentSeq << "trigger"
              << trigger;
    } else if (score >= 0) {
      qInfo() << "[PAGER_EFFECT]" << source << "seq" << currentSeq << "score"
              << score;
    } else {
      qInfo() << "[PAGER_EFFECT]" << source << "seq" << currentSeq;
    }
  }


QPointF WereadBrowser::resolveInputPos(const QPointF &inputPos, bool forward) const {
    if (inputPos.x() > 1.0 && inputPos.y() > 1.0) {
      return inputPos;
    }
    const qreal w = m_view ? m_view->width() : 954.0;
    const qreal h = m_view ? m_view->height() : 1696.0;
    const qreal x = forward ? (w * 0.85) : (w * 0.15);
    const qreal y = h * 0.5;
    return QPointF(x, y);
  }


void WereadBrowser::markPageTurnTriggered(int currentSeq, const QString &source) {
    if (m_lastPageTurnSeqNotified == currentSeq)
      return;
    m_lastPageTurnSeqNotified = currentSeq;
    qWarning() << "[PAGER] page turn triggered" << source << "seq"
               << currentSeq;
    QTimer::singleShot(200, this, [this]() { keepAliveNearChapterEnd(); });
    onPageTurnEvent();
  }


void WereadBrowser::scheduleInputFallback(bool forward, int currentSeq) {
    constexpr int kFallbackDelayMs = 900;
    QTimer::singleShot(kFallbackDelayMs, this, [this, forward, currentSeq]() {
      if (m_pendingInputFallbackSeq != currentSeq)
        return;
      if (m_inputDomObserved) {
        m_pendingInputFallbackSeq = 0;
        return;
      }
      qWarning() << "[PAGER_INPUT] no dom event after input, giving up"
                 << (forward ? "next" : "prev") << "seq" << currentSeq;
      m_pendingInputFallbackSeq = 0;
    });
  }


void WereadBrowser::runWeReadPagerJsClick(bool forward, int currentSeq, const QString &trigger) {
    if (!m_view || !m_view->page())
      return;
    const QString js =
        forward
            ? QStringLiteral("(function(){"
                             "  var "
                             "btn=document.querySelector('.renderTarget_pager_"
                             "button_right');"
                             "  if(!btn){console.log('[PAGER_JS] button not "
                             "found');return false;}"
                             "  if(!btn.click){console.log('[PAGER_JS] button "
                             "has no click method');return false;}"
                             "  var jsStartTime = new Date().getTime();"
                             "  console.log('[PAGER_JS] button found, "
                             "clicking...', jsStartTime);"
                             "  var clickStart = new Date().getTime();"
                             "  btn.click();"
                             "  var clickEnd = new Date().getTime();"
                             "  console.log('[PAGER_JS] button clicked, "
                             "duration', clickEnd - clickStart, 'ms');"
                             "  return true;"
                             "})()")
            : QStringLiteral(
                  "(function(){"
                  "  var "
                  "btn=document.querySelector('.renderTarget_pager_button');"
                  "  if(!btn){console.log('[PAGER_JS] button not "
                  "found');return false;}"
                  "  if(!btn.click){console.log('[PAGER_JS] button has no "
                  "click method');return false;}"
                  "  console.log('[PAGER_JS] button found, clicking...');"
                  "  btn.click();"
                  "  return true;"
                  "})()");
    const qint64 jsCallTime = QDateTime::currentMSecsSinceEpoch();
    qInfo() << "[PAGER_JS] dispatch" << (forward ? "next" : "prev") << "trigger"
            << trigger << "seq" << currentSeq << "ts" << jsCallTime;
    m_view->page()->runJavaScript(js, [this, currentSeq, jsCallTime, forward,
                                       trigger](const QVariant &res) {
      if (m_pendingNavSeq != currentSeq) {
        qInfo() << "[PAGER] callback cancelled (seq mismatch: pending="
                << m_pendingNavSeq << "current=" << currentSeq << ")";
        if (m_navTimeoutTimer) {
          m_navTimeoutTimer->stop();
        }
        return;
      }
      const qint64 jsResultTime = QDateTime::currentMSecsSinceEpoch();
      const qint64 jsDelay = jsResultTime - jsCallTime;
      const bool ok = res.toBool();
      qInfo() << "[PAGER]" << (forward ? "next" : "prev") << "click result"
              << ok << "seq" << currentSeq << "trigger" << trigger
              << "JS execution delay" << jsDelay << "ms";
      if (!ok) {
        if (isWeReadBook()) {
          qWarning()
              << "[PAGER]" << (forward ? "next" : "prev")
              << "button click failed, injecting mouse click to webengine";
          const QPointF centerPos(m_view ? m_view->width() / 2.0 : 477.0,
                                  m_view ? m_view->height() / 2.0 : 848.0);
          injectMouse(Qt::LeftButton, QEvent::MouseButtonPress, centerPos);
          QTimer::singleShot(50, this, [this, centerPos]() {
            injectMouse(Qt::LeftButton, QEvent::MouseButtonRelease, centerPos);
          });
          qInfo() << "[PAGER] injected mouse click at center" << centerPos;
        } else {
          const int fallback = forward ? pageStep() : -pageStep();
          qWarning() << "[PAGER]" << (forward ? "next" : "prev")
                     << "button click failed, using fallback scrollBy";
          qInfo() << "[PAGER]" << (forward ? "next" : "prev")
                  << "fallback scrollBy" << fallback;
          scrollByJs(fallback);
        }
        logPageTurnEffect(currentSeq, QStringLiteral("js-fallback"), trigger,
                          -1);
      } else {
        qInfo() << "[PAGER]" << (forward ? "next" : "prev")
                << "button click succeeded";
        logPageTurnEffect(currentSeq, QStringLiteral("js-click"), trigger, -1);
      }
      markPageTurnTriggered(currentSeq, QStringLiteral("js-click"));
      if (m_navSequence == currentSeq && m_pendingNavSeq == currentSeq) {
        m_navigating = false;
        m_pendingNavSeq = 0;
        if (m_navTimeoutTimer) {
          m_navTimeoutTimer->stop();
        }
      } else {
        qInfo() << "[PAGER]" << (forward ? "next" : "prev")
                << "callback ignored, seq mismatch (navSeq=" << m_navSequence
                << "pending=" << m_pendingNavSeq << "current=" << currentSeq
                << ")";
      }
      if (isWeReadBook() && m_weReadBookMode == QStringLiteral("mobile"))
        alignWeReadPagination();
    });
  }
