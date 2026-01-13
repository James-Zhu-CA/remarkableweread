#include "weread_browser.h"
#include <QInputDevice>
#include <QPointingDevice>
#include <QTextStream>
#include <QtTest/qtesttouch.h>

void WereadBrowser::captureFrame() { /* 一体化模式下不再抓帧到 SHM，留空 */
}

namespace {
bool isNearPos(const QPointF &a, const QPointF &b, qreal tol) {
  return qAbs(a.x() - b.x()) <= tol && qAbs(a.y() - b.y()) <= tol;
}

QPointingDevice *ensureTouchDevice() {
  static QPointingDevice *device = nullptr;
  if (!device) {
    device = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen,
                                      QInputDevice::Capability::Position |
                                          QInputDevice::Capability::Area);
  }
  return device;
}
} // namespace

void WereadBrowser::onExitClicked() { /* removed */
}

CatalogWidget *WereadBrowser::catalogWidget() const { return m_catalogWidget; }

void WereadBrowser::injectMouse(Qt::MouseButton btn, QEvent::Type type,
                                const QPointF &pos) {
  if (!m_view)
    return;
  const QPointF widgetPos = pos;
  const QPoint globalPos = m_view->mapToGlobal(widgetPos.toPoint());
  QMouseEvent *ev =
      new QMouseEvent(type, widgetPos, widgetPos, QPointF(globalPos), btn, btn,
                      Qt::NoModifier, Qt::MouseEventSynthesizedByApplication);
  QCoreApplication::postEvent(m_view, ev);
}

void WereadBrowser::injectTouch(QEvent::Type type, const QPointF &pos, int id) {
  if (!m_view)
    return;
  QPointingDevice *device = ensureTouchDevice();
  if (!device) {
    return;
  }
  QWidget *target = m_view->focusProxy();
  if (!target) {
    target = m_view;
  }
  target->setAttribute(Qt::WA_AcceptTouchEvents, true);
  const QPointF targetPos =
      (target == m_view) ? pos : target->mapFrom(m_view, pos.toPoint());
  if (type == QEvent::TouchBegin) {
    QTest::touchEvent(target, device)
        .press(id, targetPos.toPoint(), target)
        .commit();
  } else if (type == QEvent::TouchEnd) {
    QTest::touchEvent(target, device)
        .release(id, targetPos.toPoint(), target)
        .commit();
  } else {
    QTest::touchEvent(target, device)
        .move(id, targetPos.toPoint(), target)
        .commit();
  }
}

void WereadBrowser::armInjectedMousePassThrough(const QPointF &pos,
                                                int durationMs) {
  if (!m_view)
    return;
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  m_injectedMousePos = pos;
  m_injectedMouseAllowUntilMs = nowMs + durationMs;
  m_injectedMouseReplayUntilMs = 0;
}

void WereadBrowser::armInjectedTouchPassThrough(const QPointF &pos,
                                                int durationMs) {
  if (!m_view)
    return;
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  m_injectedTouchPos = pos;
  m_injectedTouchAllowUntilMs = nowMs + durationMs;
}

bool WereadBrowser::shouldReplayInjectedMouse(const QPointF &pos,
                                              qint64 nowMs) const {
  if (nowMs > m_injectedMouseAllowUntilMs)
    return false;
  return isNearPos(pos, m_injectedMousePos, 24.0);
}

void WereadBrowser::noteInjectedMouseReplayed(const QPointF &pos,
                                              qint64 nowMs) {
  m_injectedMouseReplayPos = pos;
  m_injectedMouseReplayUntilMs = nowMs + 250;
}

bool WereadBrowser::shouldBypassGestureForInjectedMouse(const QPointF &pos,
                                                        qint64 nowMs) const {
  if (nowMs > m_injectedMouseReplayUntilMs)
    return false;
  return isNearPos(pos, m_injectedMouseReplayPos, 24.0);
}

bool WereadBrowser::shouldBypassGestureForInjectedTouch(const QPointF &pos,
                                                        qint64 nowMs) const {
  if (nowMs > m_injectedTouchAllowUntilMs)
    return false;
  return isNearPos(pos, m_injectedTouchPos, 24.0);
}

void WereadBrowser::injectKey(int key) {
  if (!m_view)
    return;
  QWidget *target = m_view->focusProxy();
  if (!target)
    target = m_view;
  if (!target->hasFocus()) {
    target->setFocus(Qt::OtherFocusReason);
  }
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
  QCoreApplication::sendEvent(target, &press);
  QCoreApplication::sendEvent(target, &release);
}

QWebEngineView *WereadBrowser::view() const { return m_view; }

void WereadBrowser::scrollByJs(int dy) {
  if (!m_view || !m_view->page())
    return;
  m_view->page()->runJavaScript(
      QStringLiteral("window.scrollBy(0,%1);").arg(dy));
  qInfo() << "[TOUCH] js scroll" << dy;
  scheduleBookCaptures();
  // 使用智能刷新管理器处理滚动刷新
  if (m_smartRefresh) {
    SmartRefreshManager::RefreshEvent e;
    e.type = SmartRefreshManager::RefreshEvent::SCROLL;
    e.scrollDelta = qAbs(dy);
    QTimer::singleShot(150, this, [this, e]() {
      if (m_smartRefresh)
        m_smartRefresh->pushEvent(e);
    });
  } else if (m_fbRef) {
    // 后备：直接调用 FbRefreshHelper
    QTimer::singleShot(150, this, [this]() {
      if (m_fbRef)
        m_fbRef->refreshScroll(width(), height());
    });
  }
  // 重置空闲清理定时器
  if (m_idleCleanupTimer)
    m_idleCleanupTimer->start();
}

int WereadBrowser::pageStep() const {
  if (m_view)
    return static_cast<int>(m_view->height() * 0.3);
  return 400;
}

void WereadBrowser::hittestJs(const QPointF &pos) {
  if (!m_view || !m_view->page())
    return;
  // Fix coordinate scaling: divide by zoomFactor to get page coordinates
  const qreal zoomFactor = m_view->zoomFactor();
  const QPointF pagePos = pos / zoomFactor;
  qInfo() << "[HITTEST] Screen pos:" << pos << "Page pos:" << pagePos
          << "Zoom:" << zoomFactor;
  const QString script =
      QStringLiteral("var x=%1,y=%2;"
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
                     "  if(!(cls.includes('btn focus-btn') && "
                     "location.href.includes('/ebook/reader/'))){"
                     "    if(el.click) el.click();"
                     "  } else {"
                     "    res+='|blocked';"
                     "  }"
                     "}"
                     "res;")
          .arg(pagePos.x())
          .arg(pagePos.y());
  m_view->page()->runJavaScript(script, [](const QVariant &v) {
    qInfo() << "[HITTEST]" << v.toString();
  });
}

void WereadBrowser::injectWheel(const QPointF &pos, int dy) {
  if (!m_view)
    return;
  // Map accumulated pixel drag to wheel: 120 units per step is the Qt
  // convention. Positive dy should scroll down (natural touch).
  const int angleStep = dy;         // use pixel distance as angle seed
  QPoint pixelDelta(0, -dy);        // natural: drag down -> scroll down
  QPoint angleDelta(0, -angleStep); // same sign as pixelDelta
  auto *ev = new QWheelEvent(pos, m_view->mapToGlobal(pos.toPoint()),
                             pixelDelta, angleDelta, Qt::NoButton,
                             Qt::NoModifier, Qt::ScrollUpdate, false);
  QCoreApplication::postEvent(m_view, ev);
  qInfo() << "[TOUCH] wheel dy" << dy << "pos" << pos;
}

void WereadBrowser::goNextPage(const QPointF &inputPos) {
  qInfo() << "[PAGER] goNextPage() called"
          << "inputPos" << inputPos;
  if (!m_view) {
    qWarning() << "[PAGER] goNextPage() aborted: m_view is null";
    return;
  }
  if (!m_view->page()) {
    qWarning() << "[PAGER] goNextPage() aborted: m_view->page() is null";
    return;
  }
  const qint64 now = QDateTime::currentMSecsSinceEpoch();

  // 如果已经有翻页在进行，检查是否应该取消
  if (m_navigating) {
    const qint64 elapsed = now - m_navStartTime;
    if (elapsed > 500) { // 如果第1次点击超过500ms还没完成
      qWarning() << "[PAGER] Previous navigation taking too long (" << elapsed
                 << "ms), cancelling and processing new click. Old seq:"
                 << m_pendingNavSeq << "New seq:" << (m_navSequence + 1);

      // 取消旧的超时定时器
      if (m_navTimeoutTimer) {
        m_navTimeoutTimer->stop();
        m_navTimeoutTimer->disconnect(); // 断开所有连接，避免回调执行
        m_navTimeoutTimer->deleteLater();
        m_navTimeoutTimer = nullptr;
      }

      // 标记旧序列为已取消（回调中会检查）
      m_pendingNavSeq = 0; // 设置为0表示已取消

      // 继续处理新的点击（不return）
    } else {
      // 如果第1次点击还在500ms内，跳过第2次
      qInfo() << "[PAGER] skip next: navigating (m_navigating=true, elapsed="
              << elapsed << "ms)";
      return;
    }
  }

  const int currentSeq = ++m_navSequence;
  m_navigating = true;
  m_navStartTime = now;         // 记录开始时间
  m_pendingNavSeq = currentSeq; // 记录待处理的序列号
  cancelPendingCaptures();      // 取消旧页面的待执行抓帧
  m_firstFrameDone = false;
  m_reloadAttempts = 0;
  const qint64 tsStart = QDateTime::currentMSecsSinceEpoch();
  qInfo() << "[PAGER] next start ts" << tsStart << "seq" << currentSeq << "url"
          << m_view->url();

  // 通知智能刷新管理器：点击操作，重置score阈值（仅在书籍页面生效）
  if (m_smartRefresh) {
    m_smartRefresh->resetScoreThreshold();
  }

  if (isDedaoBook()) {
    dedaoScroll(true);
    return;
  }
  if (isWeReadBook()) {
    const bool isKindleUA =
        m_currentUA.contains(QStringLiteral("Kindle"), Qt::CaseInsensitive) ||
        m_uaWeReadBook.contains(QStringLiteral("Kindle"),
                                Qt::CaseInsensitive) ||
        m_uaMode.compare(QStringLiteral("kindle"), Qt::CaseInsensitive) == 0;
    if (m_weReadBookMode == QStringLiteral("mobile") && !isKindleUA) {
      logPageProbe(QStringLiteral("[PAGER] weRead next probe (mobile)"));
      weReadScroll(true);
      return;
    }
    qInfo() << "[PAGER] weRead kindle/web mode: using pager buttons";
  }

  // 使用成员变量的定时器，可以取消
  if (m_navTimeoutTimer) {
    m_navTimeoutTimer->stop();
    m_navTimeoutTimer->disconnect(); // 断开所有连接，避免回调执行
    m_navTimeoutTimer->deleteLater();
    m_navTimeoutTimer = nullptr; // 立即设置为nullptr，避免重复使用
  }
  m_navTimeoutTimer = new QTimer(this);
  m_navTimeoutTimer->setSingleShot(true);
  m_navTimeoutTimer->setInterval(1000);
  connect(m_navTimeoutTimer, &QTimer::timeout, this, [this, currentSeq]() {
    if (m_navigating && m_navSequence == currentSeq &&
        m_pendingNavSeq == currentSeq) {
      m_navigating = false;
      m_pendingNavSeq = 0;
      qWarning() << "[PAGER] Force unlock next (timeout 1000ms) seq"
                 << currentSeq;
    }
  });
  m_navTimeoutTimer->start();
  if (isWeReadBook()) {
    const bool isKindleUA =
        m_currentUA.contains(QStringLiteral("Kindle"), Qt::CaseInsensitive) ||
        m_uaWeReadBook.contains(QStringLiteral("Kindle"),
                                Qt::CaseInsensitive) ||
        m_uaMode.compare(QStringLiteral("kindle"), Qt::CaseInsensitive) == 0;
    if (m_weReadBookMode == QStringLiteral("mobile") && !isKindleUA) {
      runWeReadPagerJsClick(true, currentSeq,
                            QStringLiteral("mobile-fallback"));
      return;
    }
    injectKey(Qt::Key_Right);
    markPageTurnTriggered(currentSeq, QStringLiteral("input"));
    m_pendingInputFallbackSeq = currentSeq;
    m_inputFallbackStartMs = now;
    m_inputDomObserved = false;
    scheduleInputFallback(true, currentSeq);
    return;
  }
  runWeReadPagerJsClick(true, currentSeq, QStringLiteral("direct"));
}

void WereadBrowser::goPrevPage(const QPointF &inputPos) {
  qInfo() << "[PAGER] goPrevPage() called"
          << "inputPos" << inputPos;
  if (!m_view) {
    qWarning() << "[PAGER] goPrevPage() aborted: m_view is null";
    return;
  }
  if (!m_view->page()) {
    qWarning() << "[PAGER] goPrevPage() aborted: m_view->page() is null";
    return;
  }
  const qint64 now = QDateTime::currentMSecsSinceEpoch();

  // 如果已经有翻页在进行，检查是否应该取消
  if (m_navigating) {
    const qint64 elapsed = now - m_navStartTime;
    if (elapsed > 500) { // 如果第1次点击超过500ms还没完成
      qWarning() << "[PAGER] Previous navigation taking too long (" << elapsed
                 << "ms), cancelling and processing new prev click. Old seq:"
                 << m_pendingNavSeq << "New seq:" << (m_navSequence + 1);

      // 取消旧的超时定时器
      if (m_navTimeoutTimer) {
        m_navTimeoutTimer->stop();
        m_navTimeoutTimer->disconnect(); // 断开所有连接，避免回调执行
        m_navTimeoutTimer->deleteLater();
        m_navTimeoutTimer = nullptr;
      }

      // 标记旧序列为已取消（回调中会检查）
      m_pendingNavSeq = 0; // 设置为0表示已取消

      // 继续处理新的点击（不return）
    } else {
      // 如果第1次点击还在500ms内，跳过第2次
      qInfo() << "[PAGER] skip prev: navigating (m_navigating=true, elapsed="
              << elapsed << "ms)";
      return;
    }
  }

  const int currentSeq = ++m_navSequence;
  m_navigating = true;
  m_navStartTime = now;         // 记录开始时间
  m_pendingNavSeq = currentSeq; // 记录待处理的序列号
  cancelPendingCaptures();      // 取消旧页面的待执行抓帧
  m_firstFrameDone = false;
  m_reloadAttempts = 0;
  const qint64 tsStart = QDateTime::currentMSecsSinceEpoch();
  qInfo() << "[PAGER] prev start ts" << tsStart << "seq" << currentSeq << "url"
          << m_view->url();

  // 通知智能刷新管理器：点击操作，重置score阈值（仅在书籍页面生效）
  if (m_smartRefresh) {
    m_smartRefresh->resetScoreThreshold();
  }

  if (isDedaoBook()) {
    dedaoScroll(false);
    return;
  }
  if (isWeReadBook()) {
    const bool isKindleUA =
        m_currentUA.contains(QStringLiteral("Kindle"), Qt::CaseInsensitive) ||
        m_uaWeReadBook.contains(QStringLiteral("Kindle"),
                                Qt::CaseInsensitive) ||
        m_uaMode.compare(QStringLiteral("kindle"), Qt::CaseInsensitive) == 0;
    if (m_weReadBookMode == QStringLiteral("mobile") && !isKindleUA) {
      logPageProbe(QStringLiteral("[PAGER] weRead prev probe (mobile)"));
      weReadScroll(false);
      return;
    }
    qInfo() << "[PAGER] weRead kindle/web mode: using pager buttons";
  }

  // 使用成员变量的定时器，可以取消
  if (m_navTimeoutTimer) {
    m_navTimeoutTimer->stop();
    m_navTimeoutTimer->disconnect(); // 断开所有连接，避免回调执行
    m_navTimeoutTimer->deleteLater();
    m_navTimeoutTimer = nullptr; // 立即设置为nullptr，避免重复使用
  }
  m_navTimeoutTimer = new QTimer(this);
  m_navTimeoutTimer->setSingleShot(true);
  m_navTimeoutTimer->setInterval(1000);
  connect(m_navTimeoutTimer, &QTimer::timeout, this, [this, currentSeq]() {
    if (m_navigating && m_navSequence == currentSeq &&
        m_pendingNavSeq == currentSeq) {
      m_navigating = false;
      m_pendingNavSeq = 0;
      qWarning() << "[PAGER] Force unlock prev (timeout 1000ms) seq"
                 << currentSeq;
    }
  });
  m_navTimeoutTimer->start();
  if (isWeReadBook()) {
    const bool isKindleUA =
        m_currentUA.contains(QStringLiteral("Kindle"), Qt::CaseInsensitive) ||
        m_uaWeReadBook.contains(QStringLiteral("Kindle"),
                                Qt::CaseInsensitive) ||
        m_uaMode.compare(QStringLiteral("kindle"), Qt::CaseInsensitive) == 0;
    if (m_weReadBookMode == QStringLiteral("mobile") && !isKindleUA) {
      runWeReadPagerJsClick(false, currentSeq,
                            QStringLiteral("mobile-fallback"));
      return;
    }
    injectKey(Qt::Key_Left);
    markPageTurnTriggered(currentSeq, QStringLiteral("input"));
    m_pendingInputFallbackSeq = currentSeq;
    m_inputFallbackStartMs = now;
    m_inputDomObserved = false;
    scheduleInputFallback(false, currentSeq);
    return;
  }
  runWeReadPagerJsClick(false, currentSeq, QStringLiteral("direct"));
}

void WereadBrowser::openCatalog() {
  if (!m_view || !m_view->page())
    return;
  if (!isWeReadBook()) {
    qInfo() << "[MENU] catalog skipped (not WeRead book)";
    return;
  }
  if (!m_catalogWidget) {
    qWarning() << "[CATALOG] widget not ready";
    return;
  }
  if (m_catalogWidget->isVisible()) {
    m_catalogWidget->hide();
    return;
  }
  if (m_menu)
    m_menu->hide();
  fetchCatalog();
}

void WereadBrowser::openWeReadFontPanelAndSelect() {
  qInfo() << "[MENU] openWeReadFontPanelAndSelect called";
  if (!m_view || !m_view->page())
    return;
  if (isDedaoBook()) {
    toggleTheme();
    return;
  }
  if (!isWeReadBook()) {
    qInfo() << "[MENU] font panel skipped (not WeRead book)";
    return;
  }
  cancelPendingCaptures(); // 取消旧的待执行抓帧
  m_firstFrameDone = false;
  m_reloadAttempts = 0;
  const QString openPanelJs = QStringLiteral(R"WR(
(() => {
  const selectors = [
    'button.readerControls_item.fontSizeButton',
    '.readerControls_item.fontSizeButton',
    '.readerControls .fontSizeButton',
    'button[title*="字体"]',
    'button[aria-label*="字体"]'
  ];
  let btn = null;
  for (const sel of selectors) {
    const el = document.querySelector(sel);
    if (el) { btn = el; break; }
  }
  if (!btn) return {ok:false, reason:'no-font-button', selectors};
  const style = window.getComputedStyle ? getComputedStyle(btn) : null;
  const visible = style ? (style.display !== 'none' && style.visibility !== 'hidden' && style.opacity !== '0') : true;
  if (btn.click) btn.click();
  return {ok:true, visible, cls: btn.className || '', tag: btn.tagName || ''};
})()
)WR");
  m_view->page()->runJavaScript(openPanelJs, [this](const QVariant &res) {
    qInfo() << "[MENU] font panel open" << res;
  });
  QTimer::singleShot(250, this, [this]() {
    const QString selectFontJs = QStringLiteral(R"WR(
(() => {
  const targetKey = 'TsangerYunHei-W05';
  const targetName = '仓耳云黑';
  const maxTries = 8;
  const delayMs = 150;
  const normalize = (s) => (s || '').replace(/\s+/g, '').replace(/["']/g, '').toLowerCase();
  const firstFamily = (fam) => {
    if (!fam) return '';
    const parts = String(fam).split(',');
    if (!parts.length) return '';
    return parts[0].trim().replace(/^["']|["']$/g, '');
  };
  const collectMeta = (el) => {
    const styleAttr = el.getAttribute('style') || '';
    const famInline = (el.style && el.style.fontFamily) || '';
    const famComp = (window.getComputedStyle ? getComputedStyle(el).fontFamily : '') || '';
    const text = (el.innerText || el.textContent || '');
    const dataFont = el.getAttribute('data-font') || el.getAttribute('data-id') || el.getAttribute('data-key') || el.getAttribute('data-name') || '';
    const dataset = el.dataset ? Object.values(el.dataset).join(' ') : '';
    const key = (dataFont || dataset || '').trim();
    const family = firstFamily(famInline || famComp || styleAttr);
    return {styleAttr, famInline, famComp, text, dataFont, dataset, key, family};
  };
  const isTarget = (el) => {
    const meta = collectMeta(el);
    const hay = normalize(meta.styleAttr + ' ' + meta.famInline + ' ' + meta.famComp + ' ' + meta.text + ' ' + meta.dataFont + ' ' + meta.dataset);
    const hasName = hay.includes(normalize(targetName)) || hay.includes('tsangeryunhei') || hay.includes(normalize(targetKey));
    const hasW05 = hay.includes('05') || hay.includes('w05');
    return hasName && hasW05;
  };
  const fire = (el) => {
    if (!el) return;
    const rect = el.getBoundingClientRect ? el.getBoundingClientRect() : {left:0, top:0, width:0, height:0};
    const opts = {bubbles:true, cancelable:true, clientX: rect.left + rect.width / 2, clientY: rect.top + rect.height / 2};
    try { el.dispatchEvent(new PointerEvent('pointerdown', opts)); } catch(e) {}
    try { el.dispatchEvent(new PointerEvent('pointerup', opts)); } catch(e) {}
    try { el.dispatchEvent(new MouseEvent('click', opts)); } catch(e) {}
    try { if (el.click) el.click(); } catch(e) {}
  };
  const applySetting = (meta) => {
    const key = 'wrLocalSetting';
    let setting = {};
    try { setting = JSON.parse(localStorage.getItem(key) || '{}'); } catch(e) { setting = {}; }
    const before = setting.fontFamily || '';
    const preferred = meta.key || targetKey;
    if (preferred && setting.fontFamily !== preferred) {
      setting.fontFamily = preferred;
      if (!setting.fontSizeLevel) setting.fontSizeLevel = 4;
      try { localStorage.setItem(key, JSON.stringify(setting)); } catch(e) {}
    }
    return {before, after: setting.fontFamily || '', applied: setting.fontFamily !== before};
  };
  const trySelect = (tryIdx) => {
    const items = Array.from(document.querySelectorAll('.font-panel-content-fonts-item'));
    if (!items.length) {
      if (tryIdx < maxTries) return setTimeout(() => trySelect(tryIdx + 1), delayMs);
      console.log('[FONT_SELECT] no-font-items');
      return;
    }
    const target = items.find(isTarget);
    if (!target) {
      if (tryIdx < maxTries) return setTimeout(() => trySelect(tryIdx + 1), delayMs);
      const sample = items.slice(0,3).map(el => (el.getAttribute('style') || '')).join(' | ');
      console.log('[FONT_SELECT] target-not-found', items.length, sample);
      return;
    }
    if (target.scrollIntoView) target.scrollIntoView({block:'center', inline:'nearest'});
    fire(target);
    fire(target.querySelector('div, svg, span'));
    const meta = collectMeta(target);
    setTimeout(() => {
      const setting = applySetting(meta);
      const cls = target.className || '';
      const selected = cls.includes('selected') || cls.includes('active');
      console.log('[FONT_SELECT]' + JSON.stringify({
        try: tryIdx,
        selected,
        cls,
        key: meta.key,
        family: meta.family,
        before: setting.before,
        after: setting.after,
        applied: setting.applied
      }));
      if (setting.applied) {
        setTimeout(() => location.reload(), 150);
      } else if (!selected && tryIdx < maxTries) {
        setTimeout(() => trySelect(tryIdx + 1), delayMs);
      }
    }, 120);
  };
  trySelect(0);
  return {ok:true, scheduled:true, targetKey};
})()
)WR");
    m_view->page()->runJavaScript(selectFontJs, [this](const QVariant &res) {
      qInfo() << "[MENU] font select" << res;
    });
  });
}

void WereadBrowser::adjustFont(bool increase) {
  // Debounce: prevent duplicate calls within 50ms
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - m_lastFontAdjustTime < 50) {
    qInfo() << "[MENU] adjustFont debounced (duplicate call within 50ms)";
    return;
  }

  // State lock: prevent concurrent execution
  if (m_isAdjustingFont) {
    qWarning() << "[MENU] adjustFont already in progress";
    return;
  }

  m_isAdjustingFont = true;
  m_lastFontAdjustTime = now;

  qInfo() << "[MENU] adjustFont called, increase=" << increase;
  if (!m_view || !m_view->page()) {
    m_isAdjustingFont = false;
    return;
  }
  cancelPendingCaptures(); // 取消旧的待执行抓帧
  m_firstFrameDone = false;
  m_reloadAttempts = 0;
  if (isDedaoBook()) {
    const QString openSettingJs = QStringLiteral(
        "(() => {"
        "  const settingBtn = "
        "document.querySelector('.reader-tool-button.tool-margin-right');"
        "  if (settingBtn && typeof settingBtn.click === 'function') {"
        "    settingBtn.click();"
        "    return {ok:true, action:'open-setting'};"
        "  }"
        "  return {ok:false, reason:'no-setting-btn'};"
        "})();");
    m_view->page()->runJavaScript(openSettingJs, [this, increase](
                                                     const QVariant &res) {
      qInfo() << "[MENU] open setting panel" << res;
      QTimer::singleShot(300, this, [this, increase]() {
        const QString adjustFontJs =
            QStringLiteral(
                "(() => {"
                "  const inc=%1;"
                "  const buttons = "
                "Array.from(document.querySelectorAll('.set-tool-box-change-"
                "font button'));"
                "  if (buttons.length === 0) return {ok:false, "
                "reason:'no-font-buttons'};"
                "  const btn = inc>0 ? buttons[buttons.length-1] : "
                "buttons[0];"
                "  if (btn && btn.click) {"
                "    btn.click();"
                "    const fontNum = "
                "document.querySelector('.set-tool-box-font-number');"
                "    const num = fontNum ? fontNum.textContent.trim() : '';"
                "    return {ok:true, mode:'dedao', inc, font:num};"
                "  }"
                "  return {ok:false, reason:'btn-no-click', mode:'dedao'};"
                "})();")
                .arg(increase ? 1 : -1);
        m_view->page()->runJavaScript(
            adjustFontJs, [this](const QVariant &res) {
              qInfo() << "[MENU] font step (dedao)" << res;
            });
      });
    });
    m_isAdjustingFont = false;
    return;
  }
  // Simple reload approach with scroll position preservation
  const QString js =
      QStringLiteral(
          "(() => {"
          "  const delta=%1;"
          "  let setting={};"
          "  try{ "
          "setting=JSON.parse(localStorage.getItem('wrLocalSetting')||'{}'); "
          "}catch(e){ setting={}; }"
          "  const raw = setting.fontSizeLevel;"
          "  let before = parseInt(raw, 10);"
          "  if (Number.isNaN(before)) before = 4;"
          "  const after=Math.max(1, Math.min(7, before+delta));"
          "  setting.fontSizeLevel=after;"
          "  if(!setting.fontFamily) setting.fontFamily='TsangerYunHei-W05';"
          "  try{ localStorage.setItem('wrFontLevelUserSet','1'); }catch(e){}"
          "  try{ localStorage.setItem('wrLocalSetting', "
          "JSON.stringify(setting)); }catch(e){}"
          "  "
          "  try{ sessionStorage.setItem('wr_scrollY', window.scrollY || 0); "
          "}catch(e){}"
          "  "
          "  setTimeout(()=>location.reload(),50);"
          "  return {ok:true,before,after,raw,method:'reload'};"
          "})();")
          .arg(increase ? 1 : -1);

  m_view->page()->runJavaScript(js, [this](const QVariant &res) {
    qInfo() << "[MENU] font adjust" << res;
    m_isAdjustingFont = false;
  });
}

void WereadBrowser::toggleTheme() {
  qInfo() << "[MENU] toggleTheme called";
  if (!m_view || !m_view->page())
    return;
  cancelPendingCaptures(); // 取消旧的待执行抓帧
  m_firstFrameDone = false;
  m_reloadAttempts = 0;
  if (isDedaoBook()) {
    const QString openSettingJs = QStringLiteral(
        "(() => {"
        "  const settingBtn = "
        "document.querySelector('.reader-tool-button.tool-margin-right');"
        "  if (settingBtn && typeof settingBtn.click === 'function') {"
        "    settingBtn.click();"
        "    return {ok:true, action:'open-setting'};"
        "  }"
        "  return {ok:false, reason:'no-setting-btn'};"
        "})();");
    m_view->page()->runJavaScript(openSettingJs, [this](const QVariant &res) {
      qInfo() << "[MENU] open setting panel for theme" << res;
      QTimer::singleShot(300, this, [this]() {
        const QString toggleThemeJs = QStringLiteral(
            "(() => {"
            "  const list = "
            "Array.from(document.querySelectorAll('.tool-box-theme-group "
            ".reader-tool-theme-button'));"
            "  if (list.length === 0) return {ok:false, "
            "reason:'no-theme-buttons'};"
            "  const curIdx = list.findIndex(el => "
            "el.classList.contains('reader-tool-theme-button-selected'));"
            "  const nextIdx = list.length ? ((curIdx>=0 ? "
            "(curIdx+1)%list.length : 0)) : -1;"
            "  if (nextIdx>=0 && list[nextIdx] && list[nextIdx].click) {"
            "    list[nextIdx].click();"
            "    const sel = "
            "list[nextIdx].querySelector('.reader-tool-theme-button-text');"
            "    const text = sel ? sel.textContent.trim() : '';"
            "    return {ok:true, cur:curIdx, next:nextIdx, "
            "count:list.length, mode:'dedao', text};"
            "  }"
            "  return {ok:false, reason:'no-theme-btn', count:list.length, "
            "mode:'dedao'};"
            "})();");
        m_view->page()->runJavaScript(
            toggleThemeJs, [this](const QVariant &res) {
              qInfo() << "[MENU] theme toggle (dedao)" << res;
            });
      });
    });
    return;
  }
  const QString js = QStringLiteral(
      "(() => {"
      "  function getCookie(name){"
      "    return "
      "document.cookie.split(';').map(s=>s.trim()).filter(s=>s.startsWith("
      "name+'='))"
      "      .map(s=>s.substring(name.length+1))[0] || '';"
      "  }"
      "  const cookieTheme = getCookie('wr_theme');"
      "  const lsTheme = (localStorage && localStorage.getItem('wr_theme')) "
      "|| '';"
      "  const current = (cookieTheme||lsTheme||'dark').toLowerCase();"
      "  const next = current==='white' ? 'dark' : 'white';"
      "  try { document.cookie = 'wr_theme='+next+'; domain=.weread.qq.com; "
      "path=/; max-age=31536000'; } catch(e) {}"
      "  try { localStorage && localStorage.setItem('wr_theme', next); } "
      "catch(e) {}"
      "  const delay=300;"
      "  console.log('[THEME] reload scheduled in',delay,'ms');"
      "  setTimeout(()=>{ console.log('[THEME] reloading now'); "
      "location.reload(); },delay);"
      "  return {ok:true, before:current, after:next, "
      "cookieAfter:getCookie('wr_theme'), "
      "lsAfter:(localStorage&&localStorage.getItem('wr_theme'))||''};"
      "})();");
  m_view->page()->runJavaScript(js, [this](const QVariant &res) {
    qInfo() << "[MENU] theme toggle" << res;
  });
}

void WereadBrowser::toggleFontFamily() {
  if (!m_view || !m_view->page())
    return;
  m_firstFrameDone = false;
  m_reloadAttempts = 0;
  const QString js = QStringLiteral(
      "(() => {"
      "  const key='wrLocalSetting';"
      "  let setting={};"
      "  try{ setting=JSON.parse(localStorage.getItem(key)||'{}'); "
      "}catch(e){ setting={}; }"
      "  const cur=(setting.fontFamily||'TsangerYunHei-W05').toLowerCase();"
      "  const custom='custom_noto_sans';"
      "  const next = (cur===custom) ? 'TsangerYunHei-W05' : custom;"
      "  setting.fontFamily = next;"
      "  if(!setting.fontSizeLevel) setting.fontSizeLevel = 4;"
      "  try{ localStorage.setItem(key, JSON.stringify(setting)); }catch(e){}"
      "  const cssId='wr-font-override';"
      "  const exist=document.getElementById(cssId);"
      "  if (exist) exist.remove();"
      "  if (next===custom){"
      "    const style=document.createElement('style');"
      "    style.id=cssId;"
      "    style.textContent = "
      "\"@font-face{font-family:'CustomNotoSans';src:url('file:///home/root/"
      ".fonts/NotoSansCJKsc-Regular.otf') format('opentype');}\"+"
      "      \".renderTargetContent, .renderTargetContent * { "
      "font-family:'CustomNotoSans', 'Noto Sans CJK SC', 'Noto Sans SC', "
      "sans-serif !important; }\";"
      "    try{ "
      "(document.documentElement||document.body).appendChild(style); "
      "}catch(e){}"
      "  }"
      "  setTimeout(()=>location.reload(), 200);"
      "  return {ok:true,next};"
      "})();");
  m_view->page()->runJavaScript(js, [this](const QVariant &res) {
    qInfo() << "[MENU] font toggle" << res;
  });
}

void WereadBrowser::toggleService() {
  if (!m_view || !m_view->page())
    return;
  m_firstFrameDone = false;
  m_reloadAttempts = 0;
  const QString cur = m_view->url().toString();
  const bool isDedao = cur.contains(QStringLiteral("dedao.cn"));
  const QUrl next = isDedao ? QUrl(QStringLiteral("https://weread.qq.com/"))
                            : QUrl(QStringLiteral("https://www.dedao.cn/"));
  qInfo() << "[MENU] service toggle"
          << (isDedao ? "dedao->weread" : "weread->dedao") << "next" << next;
  m_view->load(next);
}

void WereadBrowser::applyDefaultLightTheme() { Q_UNUSED(m_view); }
