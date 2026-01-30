#include "weread_browser.h"

void WereadBrowser::initStartupLoad(const QUrl &url) {
  const QByteArray localHtmlEnv = qgetenv("WEREAD_LOCAL_HTML");
  const bool startBlank = qEnvironmentVariableIsSet("WEREAD_START_BLANK");
  if (startBlank) {
    qInfo() << "[WEREAD] Starting with about:blank (WEREAD_START_BLANK set)";
    m_view->setHtml(
        QStringLiteral("<!DOCTYPE html><html><body>blank start</body></html>"),
        QUrl(QStringLiteral("about:blank")));
  } else if (!localHtmlEnv.isEmpty()) {
    const QUrl localUrl =
        QUrl::fromLocalFile(QString::fromLocal8Bit(localHtmlEnv));
    qInfo() << "[WEREAD] Loading local HTML for diagnostic:" << localUrl;
    recordNavReason(QStringLiteral("startup_local_html"), localUrl);
    m_view->load(localUrl);
  } else {
    recordNavReason(QStringLiteral("startup_url"), url);
    m_view->load(url);
  }
}

void WereadBrowser::initIdleCleanup() {
  // 智能刷新：空闲清理定时器（500ms 无操作后检查是否需要 GC16 全刷清理残影）
  m_idleCleanupTimer = new QTimer(this);
  m_idleCleanupTimer->setSingleShot(true);
  m_idleCleanupTimer->setInterval(500);
  connect(m_idleCleanupTimer, &QTimer::timeout, this, [this]() {
    if (m_fbRef && m_fbRef->needsCleanup()) {
      qInfo() << "[EINK] Idle cleanup triggered: partial="
              << m_fbRef->partialCount() << "a2=" << m_fbRef->a2Count();
      m_fbRef->refreshCleanup(width(), height());
    }
  });

  // 定期保存会话状态：每分钟自动保存一次（用于书籍页面等 URL 不变的情况）
}

void WereadBrowser::initSessionAutoSave() {
  m_sessionSaveTimer = new QTimer(this);
  m_sessionSaveTimer->setSingleShot(false); // 重复触发
  m_sessionSaveTimer->setInterval(60000);   // 60秒 = 1分钟
  connect(m_sessionSaveTimer, &QTimer::timeout, this,
          &WereadBrowser::saveSessionState);
  m_sessionSaveTimer->start();
  qInfo() << "[SESSION] Auto-save timer started (interval: 60s)";
}

void WereadBrowser::initMenuOverlay() {
  // Quick menu overlay (hidden by default)
  // menu overlay placed on top of the webview so it is captured in grab()
  m_menu = new QFrame(m_view);
  m_menu->setObjectName(
      "weread_menu_overlay"); // 设置唯一标识，用于事件过滤器识别
  m_menu->setStyleSheet(
      "QFrame { background: #ffffff; border: 0px solid transparent; "
      "border-radius: 12px; }"
      "QPushButton { color: #000000; background: #e6e6e6; font-size: 22px; "
      "font-weight: 700; padding: 14px 18px; border-radius: 10px; }"
      "QPushButton:pressed { background: #cfcfcf; }");
  auto *layout = new QHBoxLayout(m_menu);
  layout->setContentsMargins(16, 12, 16, 12);
  layout->setSpacing(12);
  auto addBtn = [&](const QString &text, const std::function<void()> &fn) {
    auto *b = new QPushButton(text, m_menu);
    layout->addWidget(b);
    connect(b, &QPushButton::clicked, this, [text, fn]() {
      qInfo() << "[MENU_BUTTON] Button clicked:" << text;
      fn();
    });
    return b;
  };
  addBtn(QStringLiteral("微信读书/得到"), [this]() { toggleService(); });
  addBtn(QStringLiteral("目录"), [this]() { openCatalog(); });
  auto *fontPlus =
      addBtn(QStringLiteral("字体 +"), [this]() { adjustFont(true); });
  if (fontPlus) {
    fontPlus->setStyleSheet(
        "QPushButton { font-size:26px; font-weight:600; padding: 14px 18px; }"
        "QPushButton { letter-spacing: 2px; }");
  }
  auto *fontMinus =
      addBtn(QStringLiteral("字体 -"), [this]() { adjustFont(false); });
  if (fontMinus) {
    fontMinus->setStyleSheet(
        "QPushButton { font-size:26px; font-weight:600; padding: 14px 18px; }"
        "QPushButton { letter-spacing: 2px; }");
  }
  m_menu->setVisible(false);
  m_menu->setFixedHeight(90);
  // initial size/position; will be updated in showMenu()
  m_menu->setGeometry(20, 20, 600, 90);
  // Allow menu buttons to receive clicks - 设置属性确保菜单能拦截事件
  m_menu->setAttribute(Qt::WA_TransparentForMouseEvents,
                       false); // 确保菜单能接收鼠标事件
  m_menu->setAttribute(Qt::WA_NoMouseReplay, false); // 允许鼠标事件重放
  m_menu->raise();                                   // 确保菜单在最上层
  m_menuTimer.setInterval(5000);
  m_menuTimer.setSingleShot(true);
  connect(&m_menuTimer, &QTimer::timeout, this, [this]() {
    if (!m_menu) {
      return;
    }
    const bool wasVisible = m_menu->isVisible();
    m_menu->hide();
    if (!wasVisible) {
      return;
    }
    if (isDedaoBook()) {
      if (SmartRefreshManager *mgr = smartRefreshForPage()) {
        mgr->triggerDedaoDuRefresh(QStringLiteral("menu_hide"));
        return;
      }
    }
    if (m_fbRef) {
      m_fbRef->refreshUI(0, 0, width(), height());
    }
  });
  // Disable periodic heartbeat captures to avoid重复抓帧
  m_heartbeatTimer.setInterval(0);

  if (!m_catalogWidget && m_view) {
    m_catalogWidget = new CatalogWidget(m_view);
    connect(
        m_catalogWidget, &CatalogWidget::chapterClicked, this,
        [this](int index, const QString &uid) {
          if (!m_view || !m_view->page())
            return;
          if (isDedaoBook()) {
            handleDedaoCatalogClick(index, uid);
            return;
          }
          if (m_menu)
            m_menu->hide();
          if (m_catalogWidget)
            m_catalogWidget->hide();
          const QJsonArray uidArr{QJsonValue(uid)};
          const QString uidJson = QString::fromUtf8(
              QJsonDocument(uidArr).toJson(QJsonDocument::Compact));
          const QString openPanelJs = QStringLiteral(R"WR(
(() => {
  const forceClass = '__wr_catalog_force_show';
  const hideClass = '__wr_catalog_hide';
  const ensureStyle = () => {
    if (!document.getElementById('__wr_catalog_style')) {
      const style = document.createElement('style');
      style.id = '__wr_catalog_style';
      style.textContent =
        '.' + forceClass + '{display:block !important;visibility:visible !important;opacity:1 !important;pointer-events:auto !important;transform:none !important;}' +
        'body.' + hideClass + ' .readerCatalog{display:none !important;visibility:hidden !important;opacity:0 !important;pointer-events:none !important;}';
      (document.head || document.documentElement).appendChild(style);
    }
  };
  ensureStyle();
  if (document.body && document.body.classList.contains(hideClass)) {
    document.body.classList.remove(hideClass);
  }
  const selectors = [
    '.readerControls_item.catalog',
    '.readerControls .catalog',
    'button[title*="\u76ee\u5f55"]',
    '[data-action="catalog"]'
  ];
  const panel = document.querySelector('.readerCatalog');
  const panelVisible = panel ? (getComputedStyle(panel).display !== 'none') : false;
  let picked = null;
  for (const sel of selectors) {
    const el = document.querySelector(sel);
    if (el && !picked) {
      picked = {el, sel};
    }
  }
  const buttonRect = () => {
    if (!picked || !picked.el || !picked.el.getBoundingClientRect) return null;
    const r = picked.el.getBoundingClientRect();
    return {x: r.left + r.width / 2, y: r.top + r.height / 2, w: r.width, h: r.height};
  };
  if (panel || picked) {
    return {ok:true, selector: picked ? picked.sel : '', panelVisible, buttonRect: buttonRect()};
  }
  return {ok:false, reason:'no-catalog-panel-or-button', panelVisible};
})()
)WR");
          m_view->page()->runJavaScript(
              openPanelJs, [this](const QVariant &res) {
                qInfo() << "[CATALOG] open panel" << res;
                if (!m_view)
                  return;
                const QVariantMap map = res.toMap();
                if (!map.value(QStringLiteral("ok")).toBool())
                  return;
                const bool panelVisible =
                    map.value(QStringLiteral("panelVisible")).toBool();
                if (panelVisible)
                  return;
                const QVariantMap rect =
                    map.value(QStringLiteral("buttonRect")).toMap();
                const double x = rect.value(QStringLiteral("x")).toDouble();
                const double y = rect.value(QStringLiteral("y")).toDouble();
                const double w = rect.value(QStringLiteral("w")).toDouble();
                const double h = rect.value(QStringLiteral("h")).toDouble();
                if (!(w > 1.0 && h > 1.0))
                  return;
                const QPointF pos(x, y);
                const qreal zoom = m_view ? m_view->zoomFactor() : 1.0;
                const QPointF injectPos(pos.x() * zoom, pos.y() * zoom);
                qInfo() << "[CATALOG_NATIVE] menu touch"
                        << "pos" << pos << "injectPos" << injectPos << "zoom"
                        << zoom;
                armInjectedTouchPassThrough(injectPos);
                injectTouch(QEvent::TouchBegin, injectPos);
                QTimer::singleShot(40, this, [this, injectPos]() {
                  injectTouch(QEvent::TouchEnd, injectPos);
                });
              });
          const QString clickJs = QStringLiteral(R"WR(
(() => {
  const forceClass = '__wr_catalog_force_show';
  const hideClass = '__wr_catalog_hide';
  const ensureStyle = () => {
    if (!document.getElementById('__wr_catalog_style')) {
      const style = document.createElement('style');
      style.id = '__wr_catalog_style';
      style.textContent =
        '.' + forceClass + '{display:block !important;visibility:visible !important;opacity:1 !important;pointer-events:auto !important;transform:none !important;}' +
        'body.' + hideClass + ' .readerCatalog{display:none !important;visibility:hidden !important;opacity:0 !important;pointer-events:none !important;}';
      (document.head || document.documentElement).appendChild(style);
    }
  };
  const ensureForce = (panel) => {
    if (!panel) return false;
    ensureStyle();
    panel.classList.add(forceClass);
    panel.setAttribute('data-wr-catalog-forced', '1');
    return true;
  };
  if (document.body && document.body.classList.contains(hideClass)) {
    document.body.classList.remove(hideClass);
  }
  const matchTitleRaw = (%1[0] || '');
  const targetIdx = %2;
  const normalize = (s) => String(s || '').replace(/\s+/g, ' ').trim();
  const matchTitle = normalize(matchTitleRaw);
  const items = Array.from(document.querySelectorAll('.readerCatalog_list_item'));
  if (!items.length) {
    return {ok:false, reason:'no-items', matchTitleRaw, targetIdx, count:0};
  }
  const getTitle = (item) => {
    const titleEl = item.querySelector('.readerCatalog_list_item_title_text');
    let title = titleEl ? titleEl.innerText : '';
    if (!title) title = item.innerText || '';
    return normalize(title);
  };
  let target = null;
  let method = '';
  let matchCount = 0;
  if (matchTitle) {
    const matches = [];
    items.forEach((item, idx) => {
      const t = getTitle(item);
      if (t && t === matchTitle) {
        matches.push({item, idx});
      }
    });
    matchCount = matches.length;
    if (matches.length === 1) {
      target = matches[0].item;
      method = 'title';
    } else if (matches.length > 1) {
      const byIndex = matches.find(m => m.idx === targetIdx);
      if (byIndex) {
        target = byIndex.item;
        method = 'title+index';
      }
    }
  }
  if (!target) {
    return {ok:false, reason:'no-target', matchTitleRaw, targetIdx, matchCount,
            count: items.length};
  }
  try { if (target.scrollIntoView) target.scrollIntoView({block:'center'}); } catch(e) {}
  const inner = target.querySelector('.readerCatalog_list_item_inner') || target;
  if (!inner || !inner.getBoundingClientRect) {
    return {ok:false, reason:'no-rect', matchTitleRaw, targetIdx, matchCount};
  }
  const panel = document.querySelector('.readerCatalog');
  let r = inner.getBoundingClientRect();
  let rect = {x: r.left + r.width / 2, y: r.top + r.height / 2, w: r.width, h: r.height};
  if (!(rect.w > 1 && rect.h > 1) && panel) {
    ensureForce(panel);
    r = inner.getBoundingClientRect();
    rect = {x: r.left + r.width / 2, y: r.top + r.height / 2, w: r.width, h: r.height};
  }
  if (!(rect.w > 1 && rect.h > 1)) {
    return {ok:false, reason:'rect-empty', rect, matchTitleRaw, targetIdx, matchCount};
  }
  const list = target.closest('.readerCatalog_list') || document.querySelector('.readerCatalog_list');
  if (list && list.getBoundingClientRect) {
    const listRect = list.getBoundingClientRect();
    if (rect.y < listRect.top || rect.y > listRect.bottom) {
      const targetCenter = listRect.top + listRect.height / 2;
      const delta = rect.y - targetCenter;
      list.scrollTop = Math.max(0, list.scrollTop + delta);
      r = inner.getBoundingClientRect();
      rect = {x: r.left + r.width / 2, y: r.top + r.height / 2, w: r.width, h: r.height};
    }
  }
  const vh = window.innerHeight || document.documentElement.clientHeight || 0;
  if (vh && (rect.y < 0 || rect.y > vh)) {
    return {ok:false, reason:'offscreen', rect, matchTitleRaw, targetIdx, matchCount};
  }
  
  // Force jump to chapter start (remove page offset from chapterUid)
  const getChapterLink = (item) => {
    const link = item.querySelector('a[href*="/web/reader/"]');
    if (link && link.href) {
      const match = link.href.match(/\/web\/reader\/([^k]+k)([^#?]+)/);
      if (match) {
        const bookId = match[1];
        const fullUid = match[2];
        const baseUid = fullUid.split('_')[0].split('#')[0];
        return {baseUrl: `/web/reader/${bookId}${baseUid}`, fullUid, baseUid};
      }
    }
    return null;
  };
  
  const linkInfo = getChapterLink(target);
  if (linkInfo && linkInfo.baseUrl) {
    setTimeout(() => {
      try {
        location.assign(linkInfo.baseUrl);
      } catch(e) {
        console.log('[CATALOG] navigate error', e);
      }
    }, 100);
    return {
      ok: true, 
      method: method + '+forced-start', 
      matchTitleRaw, 
      targetIdx, 
      matchCount, 
      count: items.length, 
      rect,
      linkInfo
    };
  }
  
  return {ok:true, method, matchTitleRaw, targetIdx, matchCount, count: items.length, rect};
})()
)WR")
                                      .arg(uidJson)
                                      .arg(index);
          const quint64 clickSeq = ++m_catalogClickSeq;
          const int maxRetries = 3;
          auto runAttempt = [this, clickJs, clickSeq,
                             maxRetries](auto &&self, int retryCount) -> void {
            if (!m_view || !m_view->page() || clickSeq != m_catalogClickSeq)
              return;
            m_view->page()->runJavaScript(
                clickJs, [this, clickJs, clickSeq, maxRetries, retryCount,
                          self](const QVariant &res) mutable {
                  if (clickSeq != m_catalogClickSeq)
                    return;
                  qInfo() << "[CATALOG_NATIVE] find" << res;
                  const QVariantMap map = res.toMap();
                  if (!map.value(QStringLiteral("ok")).toBool()) {
                    const QString reason =
                        map.value(QStringLiteral("reason")).toString();
                    const bool shouldRetry =
                        reason == QStringLiteral("offscreen") ||
                        reason == QStringLiteral("rect-empty") ||
                        reason == QStringLiteral("no-items");
                    if (shouldRetry && retryCount < maxRetries) {
                      qInfo() << "[CATALOG_NATIVE] retry" << (retryCount + 1)
                              << "reason" << reason;
                      QTimer::singleShot(300, this,
                                         [self, retryCount]() mutable {
                                           self(self, retryCount + 1);
                                         });
                      return;
                    }
                    qWarning() << "[CATALOG_NATIVE] find failed" << map;
                    return;
                  }
                  const QVariantMap rect =
                      map.value(QStringLiteral("rect")).toMap();
                  const double x = rect.value(QStringLiteral("x")).toDouble();
                  const double y = rect.value(QStringLiteral("y")).toDouble();
                  const double w = rect.value(QStringLiteral("w")).toDouble();
                  const double h = rect.value(QStringLiteral("h")).toDouble();
                  if (!(w > 1.0 && h > 1.0)) {
                    qWarning() << "[CATALOG_NATIVE] target rect invalid" << rect
                               << "meta" << map;
                    return;
                  }
                  if (m_view && !m_view->hasFocus()) {
                    m_view->setFocus(Qt::OtherFocusReason);
                  }
                  const QPointF pos(x, y);
                  const qreal zoom = m_view ? m_view->zoomFactor() : 1.0;
                  const QPointF injectPos(pos.x() * zoom, pos.y() * zoom);
                  qInfo() << "[CATALOG_NATIVE] click"
                          << "pos" << pos << "injectPos" << injectPos << "zoom"
                          << zoom << "method"
                          << map.value(QStringLiteral("method")).toString()
                          << "matchCount"
                          << map.value(QStringLiteral("matchCount")).toInt()
                          << "targetIdx"
                          << map.value(QStringLiteral("targetIdx")).toInt();
                  qInfo() << "[CATALOG_NATIVE] touch inject";
                  armInjectedTouchPassThrough(injectPos);
                  injectTouch(QEvent::TouchBegin, injectPos);
                  QTimer::singleShot(40, this, [this, injectPos]() {
                    injectTouch(QEvent::TouchEnd, injectPos);
                  });
                  const QString hidePanelJs = QStringLiteral(R"WR(
(() => {
  const forceClass = '__wr_catalog_force_show';
  const hideClass = '__wr_catalog_hide';
  const ensureStyle = () => {
    if (!document.getElementById('__wr_catalog_style')) {
      const style = document.createElement('style');
      style.id = '__wr_catalog_style';
      style.textContent =
        '.' + forceClass + '{display:block !important;visibility:visible !important;opacity:1 !important;pointer-events:auto !important;transform:none !important;}' +
        'body.' + hideClass + ' .readerCatalog{display:none !important;visibility:hidden !important;opacity:0 !important;pointer-events:none !important;}';
      (document.head || document.documentElement).appendChild(style);
    }
  };
  const panel = document.querySelector('.readerCatalog');
  const body = document.body;
  if (!panel || !body) return {ok:false, reason:'no-panel'};
  ensureStyle();
  body.classList.add(hideClass);
  panel.classList.remove(forceClass);
  panel.removeAttribute('data-wr-catalog-forced');
  return {ok:true, hidden:true};
})()
)WR");
                  QTimer::singleShot(180, this, [this, hidePanelJs]() {
                    if (!m_view || !m_view->page())
                      return;
                    m_view->page()->runJavaScript(
                        hidePanelJs, [](const QVariant &res) {
                          qInfo() << "[CATALOG_NATIVE] hide panel" << res;
                        });
                  });
                });
          };
          QTimer::singleShot(
              250, this, [runAttempt]() mutable { runAttempt(runAttempt, 0); });
        });
    connect(m_catalogWidget, &CatalogWidget::closed, this, [this]() {
      if (m_fbRef)
        m_fbRef->refreshUI(0, 0, width(), height());
    });
    connect(m_catalogWidget, &CatalogWidget::requestRefresh, this, [this]() {
      if (m_fbRef)
        m_fbRef->refreshUI(0, 0, width(), height());
    });
  }
}

void WereadBrowser::initStateResponder() {
  // listen for state requests on 45457 and respond with current state
  const QHostAddress bindAddr(QStringLiteral("127.0.0.1"));
  const bool ok = m_stateResponder.bind(
      bindAddr, 45457, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
  qInfo() << "[STATE] responder bind" << ok << "addr" << bindAddr.toString()
          << "port" << 45457;
  connect(&m_stateResponder, &QUdpSocket::readyRead, this,
          &WereadBrowser::handleStateRequest);

  // 暂停所有注入脚本（探针/错误钩子），验证是否为注入引起的解析问题
}
