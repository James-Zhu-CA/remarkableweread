#include "weread_browser.h"
#include <QTextStream>
#include <QUrlQuery>

namespace {
const char kDedaoCatalogOpenScript[] = R"WR(
(() => {
  const hideClass = '__dedao_catalog_hide';
  if (document.body && document.body.classList.contains(hideClass)) {
    document.body.classList.remove(hideClass);
  }
  const list = document.querySelector('.iget-reader-catalog-content');
  const items = list ? list.querySelectorAll('li') : null;
  const listCount = items ? items.length : 0;
  const wrapper = document.querySelector('.iget-reader-catalog-wrapper');
  const panelVisible = (() => {
    if (!wrapper || !window.getComputedStyle) return false;
    const style = getComputedStyle(wrapper);
    return style.display !== 'none' && style.visibility !== 'hidden' && style.opacity !== '0';
  })();
  if (listCount > 0) {
    return {ok:true, action:'already-open', listCount, panelVisible};
  }
  const textMatch = (el, key) => {
    const text = (el && el.innerText) ? el.innerText.trim() : '';
    return text.indexOf(key) !== -1;
  };
  let btn = null;
  const icon = document.querySelector('.iget-icon-list');
  if (icon && icon.closest) {
    btn = icon.closest('button');
  }
  if (!btn) {
    const buttons = Array.from(document.querySelectorAll('button, [role=button]'));
    btn = buttons.find(b => textMatch(b, '目录')) || null;
  }
  let tab = null;
  const tabs = Array.from(document.querySelectorAll('.slide-tags-item'));
  tab = tabs.find(t => textMatch(t, '目录')) || null;
  if (tab && tab.click) tab.click();
  if (btn && btn.click) {
    btn.click();
    return {ok:true, action:'clicked', listCount, panelVisible, hasTab: !!tab};
  }
  if (tab) {
    return {ok:true, action:'tab-clicked', listCount, panelVisible, hasTab: true};
  }
  return {ok:false, reason:'no-btn', listCount, panelVisible};
})()
)WR";

const char kDedaoCatalogHideScript[] = R"WR(
(() => {
  const hideClass = '__dedao_catalog_hide';
  const styleId = '__dedao_catalog_hide_style';
  if (!document.getElementById(styleId)) {
    const style = document.createElement('style');
    style.id = styleId;
    style.textContent =
      'body.' + hideClass + ' .iget-reader-catalog-wrapper{display:none !important;visibility:hidden !important;opacity:0 !important;pointer-events:none !important;}' +
      'body.' + hideClass + ' .slide-wrapper-tags{display:none !important;visibility:hidden !important;opacity:0 !important;pointer-events:none !important;}';
    (document.head || document.documentElement).appendChild(style);
  }
  if (document.body) {
    document.body.classList.add(hideClass);
    return {ok:true};
  }
  return {ok:false, reason:'no-body'};
})()
)WR";

const char kDedaoCatalogCurrentScript[] = R"WR(
(() => {
  const normalize = (s) => String(s || '').replace(/\s+/g, ' ').trim();
  const selected =
    document.querySelector('.iget-reader-catalog-item-selected .iget-reader-catalog-item-text') ||
    document.querySelector('.iget-reader-catalog-item-selected');
  if (!selected) return {ok:false, reason:'no-selected'};
  const title = normalize(selected.innerText || '');
  if (!title) return {ok:false, reason:'empty-title'};
  return {ok:true, title};
})()
)WR";

const char kDedaoCatalogScrollStepScript[] = R"WR(
(() => {
  const stepPx = %1;
  const reset = %2;
  const normalize = (s) => String(s || '').replace(/\s+/g, ' ').trim();
  const tabs = Array.from(document.querySelectorAll('.slide-tags-item'));
  const tab = tabs.find(t => normalize(t.innerText) === '\u76ee\u5f55');
  if (tab && tab.classList && !tab.classList.contains('slide-tags-item-active') && tab.click) {
    tab.click();
  }
  const list = document.querySelector('.iget-reader-catalog-content');
  if (!list) return {ok:false, reason:'no-list'};
  const findScroller = (el) => {
    let cur = el;
    for (let i = 0; i < 6 && cur; i++) {
      if (cur.scrollHeight > cur.clientHeight + 10) return cur;
      cur = cur.parentElement;
    }
    return document.scrollingElement || document.documentElement || null;
  };
  const scroller = findScroller(list);
  if (!scroller) return {ok:false, reason:'no-scroller'};
  if (reset) {
    scroller.scrollTop = 0;
  } else if (stepPx > 0) {
    const maxTop = Math.max(0, scroller.scrollHeight - scroller.clientHeight);
    scroller.scrollTop = Math.min(scroller.scrollTop + stepPx, maxTop);
  }
  const items = Array.from(list.querySelectorAll('li'));
  let expanded = 0;
  items.forEach((item) => {
    const icon = item.querySelector('.iget-icon-play');
    if (!icon) return;
    if (icon.classList.contains('catalog-item-icon-actived')) return;
    try { if (icon.click) icon.click(); expanded++; } catch (e) {}
  });
  return {
    ok:true,
    scrollTop: scroller.scrollTop || 0,
    scrollHeight: scroller.scrollHeight || 0,
    clientHeight: scroller.clientHeight || 0,
    expanded,
    stepPx
  };
})()
)WR";

const char kDedaoCatalogScrapeScript[] = R"WR(
(() => {
  const list = document.querySelector('.iget-reader-catalog-content');
  const items = Array.from(list ? list.querySelectorAll('li') : []);
  if (!items.length) return {ok:false, reason:'no-items'};
  const normalize = (s) => String(s || '').replace(/\s+/g, ' ').trim();
  let minPad = null;
  const padList = items.map((item) => {
    let pad = NaN;
    if (item.style && item.style.paddingLeft) {
      pad = parseInt(item.style.paddingLeft, 10);
    }
    if (!Number.isFinite(pad) && window.getComputedStyle) {
      pad = parseInt(getComputedStyle(item).paddingLeft, 10);
    }
    if (!Number.isFinite(pad)) pad = 0;
    if (minPad === null || pad < minPad) minPad = pad;
    return pad;
  });
  if (!Number.isFinite(minPad)) minPad = 0;
  const step = 20;
  const toLevel = (pad) => {
    const delta = pad - minPad;
    const level = Math.round(delta / step) + 1;
    return level > 0 ? level : 1;
  };
  const chapters = items.map((item, idx) => {
    const titleEl = item.querySelector('.iget-reader-catalog-item-text');
    let title = titleEl ? titleEl.innerText : '';
    if (!title) title = item.innerText || '';
    title = normalize(title);
    const isCurrent = item.classList.contains('iget-reader-catalog-item-selected');
    const pad = padList[idx] || 0;
    return {
      index: idx,
      title,
      level: toLevel(pad),
      isCurrent,
      matchKey: title,
      chapterUid: title
    };
  }).filter(ch => ch && ch.title);
  return {ok:true, chapters, count: chapters.length, source:'dedao-dom'};
})()
)WR";

const char kDedaoCatalogClickScript[] = R"WR(
(() => {
  const matchTitleRaw = (%1[0] || '');
  const targetIdx = %2;
  const normalize = (s) => String(s || '').replace(/\s+/g, ' ').trim();
  const list = document.querySelector('.iget-reader-catalog-content');
  const items = Array.from(list ? list.querySelectorAll('li') : []);
  if (!items.length) return {ok:false, reason:'no-items'};
  const getTitle = (item) => {
    const titleEl = item.querySelector('.iget-reader-catalog-item-text');
    let title = titleEl ? titleEl.innerText : '';
    if (!title) title = item.innerText || '';
    return normalize(title);
  };
  const matchTitle = normalize(matchTitleRaw);
  let target = null;
  let method = '';
  let matchCount = 0;
  if (matchTitle) {
    items.forEach((item, idx) => {
      const t = getTitle(item);
      if (t && t === matchTitle) {
        matchCount++;
        if (!target || idx === targetIdx) {
          target = item;
          method = idx === targetIdx ? 'title+index' : 'title';
        }
      }
    });
  }
  if (!target && targetIdx >= 0 && targetIdx < items.length) {
    target = items[targetIdx];
    method = 'index';
  }
  if (!target) {
    return {ok:false, reason:'no-target', matchTitleRaw, targetIdx, matchCount, count: items.length};
  }
  const tab = Array.from(document.querySelectorAll('.slide-tags-item'))
    .find(el => normalize(el.innerText) === '目录');
  if (tab && tab.classList && !tab.classList.contains('slide-tags-item-active') && tab.click) {
    tab.click();
  }
  try { if (target.scrollIntoView) target.scrollIntoView({block:'center'}); } catch (e) {}
  const clickable = target.querySelector('.iget-reader-catalog-item-text') || target;
  if (!clickable || !clickable.getBoundingClientRect) {
    return {ok:false, reason:'no-rect', matchTitleRaw, targetIdx, matchCount};
  }
  const r = clickable.getBoundingClientRect();
  const rect = {x: r.left + r.width / 2, y: r.top + r.height / 2, w: r.width, h: r.height};
  if (!(rect.w > 1 && rect.h > 1)) {
    return {ok:false, reason:'rect-empty', rect, matchTitleRaw, targetIdx, matchCount};
  }
  const fire = (el) => {
    try { el.dispatchEvent(new PointerEvent('pointerdown', {bubbles:true,cancelable:true})); } catch (e) {}
    try { el.dispatchEvent(new PointerEvent('pointerup', {bubbles:true,cancelable:true})); } catch (e) {}
    try { el.dispatchEvent(new MouseEvent('click', {bubbles:true,cancelable:true})); } catch (e) {}
    try { if (el.click) el.click(); } catch (e) {}
  };
  fire(clickable);
  return {ok:true, method, rect, matchCount, targetIdx};
})()
)WR";

QString dedaoCatalogKeyForUrl(const QUrl &url) {
  if (!url.host().contains(QStringLiteral("dedao.cn"),
                           Qt::CaseInsensitive)) {
    return QString();
  }
  if (!url.path().contains(QStringLiteral("/ebook/reader")))
    return QString();
  const QUrlQuery query(url);
  const QString id = query.queryItemValue(QStringLiteral("id"));
  if (!id.isEmpty())
    return id;
  return url.toString();
}
} // namespace

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

  auto dispatchKey = [this, currentSeq, forward]() {
    if (!m_view || !m_view->page()) {
      m_navigating = false;
      return;
    }
    const qint64 ts = QDateTime::currentMSecsSinceEpoch();
    const int key = forward ? Qt::Key_PageDown : Qt::Key_PageUp;
    const QString keyName =
        forward ? QStringLiteral("PageDown") : QStringLiteral("PageUp");
    qInfo() << "[PAGER] dedao key dispatch seq" << currentSeq << "forward"
            << forward << "key" << keyName << "ts" << ts;
    injectKeyWithModifiers(key, Qt::NoModifier);

    if (m_navSequence == currentSeq) {
      m_navigating = false;
      if (m_smartRefreshDedao) {
        qInfo() << "[SMART_REFRESH] dedao page turn trigger seq" << currentSeq
                << "source key";
        m_smartRefreshDedao->triggerPageTurn();
      }
    }
  };

  const QString clickSeqJs = QStringLiteral(R"(
(() => {
  if (window.__SMART_REFRESH_DEDAO_CLICK_BASELINE__) {
    return window.__SMART_REFRESH_DEDAO_CLICK_BASELINE__();
  }
  if (window.__SMART_REFRESH_DEDAO_CLICK_SEQ_INC__) {
    return window.__SMART_REFRESH_DEDAO_CLICK_SEQ_INC__();
  }
  return null;
})()
  )");
  m_view->page()->runJavaScript(
      clickSeqJs,
      [this, currentSeq, dispatchKey](const QVariant &v) mutable {
        if (m_navSequence != currentSeq) {
          return;
        }
        if (v.isNull()) {
          qInfo() << "[SMART_REFRESH] dedao clickSeq update skipped";
        } else {
          qInfo() << "[SMART_REFRESH] dedao clickSeq update" << v;
        }
        dispatchKey();
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


void WereadBrowser::openDedaoCatalog() {
  if (!m_view || !m_view->page())
    return;
  if (!isDedaoBook()) {
    qInfo() << "[MENU] catalog skipped (not Dedao book)";
    return;
  }
  if (!m_catalogWidget) {
    qWarning() << "[CATALOG_DEDAO] widget not ready";
    return;
  }
  if (m_catalogWidget->isVisible()) {
    m_catalogWidget->hide();
    return;
  }
  if (m_menu)
    m_menu->hide();
  fetchDedaoCatalog();
}

void WereadBrowser::fetchDedaoCatalog() {
  if (!m_view || !m_view->page())
    return;
  const QString cacheKey = dedaoCatalogKeyForUrl(m_view->url());
  if (!cacheKey.isEmpty() && m_dedaoCatalogCacheKey == cacheKey &&
      !m_dedaoCatalogCache.isEmpty()) {
    qInfo() << "[CATALOG_DEDAO] using cache" << m_dedaoCatalogCache.size()
            << "key" << cacheKey;
    const QString openJs = QString::fromUtf8(kDedaoCatalogOpenScript);
    m_view->page()->runJavaScript(openJs, [](const QVariant &res) {
      qInfo() << "[CATALOG_DEDAO] open panel" << res;
    });
    QTimer::singleShot(200, this, [this]() {
      if (!m_view || !m_view->page())
        return;
      const QString currentJs = QString::fromUtf8(kDedaoCatalogCurrentScript);
      m_view->page()->runJavaScript(currentJs, [this](const QVariant &v) {
        const QVariantMap map = v.toMap();
        const bool ok = map.value(QStringLiteral("ok")).toBool();
        if (ok) {
          const QString title = map.value(QStringLiteral("title")).toString();
          const QString target = title.simplified();
          bool found = false;
          QJsonArray updated;
          for (const auto &item : m_dedaoCatalogCache) {
            QJsonObject obj = item.toObject();
            const QString t = obj.value(QStringLiteral("title"))
                                  .toString()
                                  .simplified();
            const bool match = !target.isEmpty() && t == target;
            const bool mark = match && !found;
            if (mark)
              found = true;
            obj.insert(QStringLiteral("isCurrent"), mark);
            updated.append(obj);
          }
          if (found) {
            m_dedaoCatalogCache = updated;
            qInfo() << "[CATALOG_DEDAO] current chapter updated" << target;
          } else {
            qInfo() << "[CATALOG_DEDAO] current chapter not found" << target;
          }
        } else {
          qInfo() << "[CATALOG_DEDAO] current chapter missing" << map;
        }
        if (m_catalogWidget) {
          m_catalogWidget->loadChapters(m_dedaoCatalogCache);
          hideDedaoNativeCatalog();
        }
      });
    });
    return;
  }
  if (m_dedaoCatalogScanning) {
    qInfo() << "[CATALOG_DEDAO] scan already running";
    return;
  }
  m_dedaoCatalogScanning = true;
  m_dedaoCatalogScanRounds = 0;
  m_dedaoCatalogScanStepPx = 0;
  m_dedaoCatalogScanKey = cacheKey;
  qInfo() << "[CATALOG_DEDAO] Fetching starting...";
  const QString openJs = QString::fromUtf8(kDedaoCatalogOpenScript);
  m_view->page()->runJavaScript(openJs, [](const QVariant &res) {
    qInfo() << "[CATALOG_DEDAO] open panel" << res;
  });

  QTimer::singleShot(200, this,
                     [this]() { advanceDedaoCatalogScan(true); });
}

void WereadBrowser::advanceDedaoCatalogScan(bool reset) {
  if (!m_view || !m_view->page() || !isDedaoBook()) {
    m_dedaoCatalogScanning = false;
    return;
  }
  const int stepPx = m_dedaoCatalogScanStepPx > 0
                         ? m_dedaoCatalogScanStepPx
                         : 240;
  const QString scanJs = QString::fromUtf8(kDedaoCatalogScrollStepScript)
                             .arg(stepPx)
                             .arg(reset ? 1 : 0);
  m_view->page()->runJavaScript(scanJs, [this](const QVariant &v) {
    const QVariantMap resMap = v.toMap();
    if (!resMap.value(QStringLiteral("ok")).toBool()) {
      qWarning() << "[CATALOG_DEDAO] scan failed" << resMap;
      m_dedaoCatalogScanning = false;
      scheduleDedaoCatalogScrape(500);
      return;
    }
    const int clientHeight = resMap.value(QStringLiteral("clientHeight")).toInt();
    const int scrollHeight = resMap.value(QStringLiteral("scrollHeight")).toInt();
    const int scrollTop = resMap.value(QStringLiteral("scrollTop")).toInt();
    const int expanded = resMap.value(QStringLiteral("expanded")).toInt();
    if (scrollHeight > m_dedaoCatalogMaxScrollHeight) {
      m_dedaoCatalogMaxScrollHeight = scrollHeight;
      m_dedaoCatalogStableScrollHeightRounds = 0;
    } else if (scrollHeight == m_dedaoCatalogMaxScrollHeight) {
      m_dedaoCatalogStableScrollHeightRounds++;
    }
    if (m_dedaoCatalogScanStepPx <= 0 && clientHeight > 0) {
      m_dedaoCatalogScanStepPx =
          qMax(240, int(clientHeight * 0.7));
    }
    qInfo() << "[CATALOG_DEDAO] scan step" << m_dedaoCatalogScanRounds
            << "scrollTop" << scrollTop << "scrollHeight" << scrollHeight
            << "expanded" << expanded << "stableHeightRounds"
            << m_dedaoCatalogStableScrollHeightRounds;
    m_dedaoCatalogScanRounds++;
    const bool atEnd = clientHeight > 0
                           ? (scrollTop + clientHeight >= scrollHeight - 2)
                           : false;
    if ((atEnd && m_dedaoCatalogStableScrollHeightRounds >= 2) ||
        m_dedaoCatalogScanRounds > 260) {
      m_dedaoCatalogScanning = false;
      scheduleDedaoCatalogScrape(500);
      return;
    }
    const int delayMs = atEnd ? 350 : 200;
    QTimer::singleShot(delayMs, this,
                       [this]() { advanceDedaoCatalogScan(false); });
  });
}

void WereadBrowser::scheduleDedaoCatalogScrape(int delayMs) {
  if (delayMs < 200)
    delayMs = 200;
  QTimer::singleShot(delayMs, this, [this]() {
    if (!m_view || !m_view->page() || !isDedaoBook()) {
      m_dedaoCatalogScanning = false;
      return;
    }
    const QString scrapeJs = QString::fromUtf8(kDedaoCatalogScrapeScript);
    m_view->page()->runJavaScript(scrapeJs, [this](const QVariant &v) {
      QVariantMap resMap = v.toMap();
      if (!resMap.value(QStringLiteral("ok")).toBool()) {
        qWarning() << "[CATALOG_DEDAO] scrape failed" << resMap;
        m_dedaoCatalogScanning = false;
        return;
      }
      const QVariantList chapterList = resMap.value("chapters").toList();
      qInfo() << "[CATALOG_DEDAO] Got" << chapterList.size() << "chapters";
      QJsonArray chapters;
      for (const auto &item : chapterList) {
        chapters.append(QJsonObject::fromVariantMap(item.toMap()));
      }
      const QString cacheKey = dedaoCatalogKeyForUrl(m_view->url());
      if (!cacheKey.isEmpty() && cacheKey == m_dedaoCatalogScanKey) {
        m_dedaoCatalogCacheKey = cacheKey;
        m_dedaoCatalogCache = chapters;
        qInfo() << "[CATALOG_DEDAO] cache stored" << m_dedaoCatalogCache.size()
                << "key" << cacheKey;
      }
      if (m_catalogWidget) {
        m_catalogWidget->loadChapters(chapters);
        qInfo() << "[CATALOG_DEDAO] Widget shown, visible:"
                << m_catalogWidget->isVisible();
        hideDedaoNativeCatalog();
      } else {
        qWarning() << "[CATALOG_DEDAO] m_catalogWidget is null!";
      }
      m_dedaoCatalogScanning = false;
    });
  });
}

void WereadBrowser::hideDedaoNativeCatalog() {
  if (!m_view || !m_view->page())
    return;
  const QString hideJs = QString::fromUtf8(kDedaoCatalogHideScript);
  m_view->page()->runJavaScript(hideJs, [](const QVariant &res) {
    qInfo() << "[CATALOG_DEDAO] hide native" << res;
  });
}

void WereadBrowser::handleDedaoCatalogClick(int index, const QString &uid) {
  if (!m_view || !m_view->page())
    return;
  if (m_menu)
    m_menu->hide();
  if (m_catalogWidget)
    m_catalogWidget->hide();
  const QString openPanelJs = QString::fromUtf8(kDedaoCatalogOpenScript);
  m_view->page()->runJavaScript(openPanelJs, [](const QVariant &res) {
    qInfo() << "[CATALOG_DEDAO] open panel" << res;
  });

  const QJsonArray uidArr{QJsonValue(uid)};
  const QString uidJson = QString::fromUtf8(
      QJsonDocument(uidArr).toJson(QJsonDocument::Compact));
  const QString clickJs =
      QString::fromUtf8(kDedaoCatalogClickScript).arg(uidJson).arg(index);
  const quint64 clickSeq = ++m_catalogClickSeq;
  const int maxRetries = 2;
  auto runAttempt = [this, clickSeq, clickJs, openPanelJs,
                     maxRetries](auto &&self, int retryCount) -> void {
    if (!m_view || !m_view->page() || clickSeq != m_catalogClickSeq)
      return;
    m_view->page()->runJavaScript(
        clickJs, [this, clickSeq, openPanelJs, maxRetries, retryCount,
                  self](const QVariant &res) mutable {
          if (clickSeq != m_catalogClickSeq)
            return;
          qInfo() << "[CATALOG_DEDAO] click" << res;
          const QVariantMap map = res.toMap();
          if (map.value(QStringLiteral("ok")).toBool()) {
            m_dedaoCatalogJumpPending = true;
            if (m_smartRefreshDedao) {
              m_smartRefreshDedao->triggerDedaoDuRefresh(
                  QStringLiteral("catalog_jump"));
              m_smartRefreshDedao->scheduleDedaoFallbackSeries();
            }
            QTimer::singleShot(200, this,
                               [this]() { hideDedaoNativeCatalog(); });
            return;
          }
          const QString reason = map.value(QStringLiteral("reason")).toString();
          const bool shouldRetry =
              reason == QStringLiteral("no-items") ||
              reason == QStringLiteral("no-rect") ||
              reason == QStringLiteral("rect-empty") ||
              reason == QStringLiteral("offscreen");
          if (!shouldRetry || retryCount >= maxRetries)
            return;
          m_view->page()->runJavaScript(openPanelJs, [](const QVariant &res) {
            qInfo() << "[CATALOG_DEDAO] reopen panel" << res;
          });
          QTimer::singleShot(300, this, [self, retryCount]() mutable {
            self(self, retryCount + 1);
          });
        });
  };
  QTimer::singleShot(250, this,
                     [runAttempt]() mutable { runAttempt(runAttempt, 0); });
}


void WereadBrowser::recordNavReason(const QString &reason,
                                    const QUrl &target) {
    m_lastNavReason = reason;
    m_lastNavReasonTarget = target;
    m_lastNavReasonTs = QDateTime::currentMSecsSinceEpoch();
    qInfo() << "[NAV_REASON] set" << reason << "target" << target << "from"
            << currentUrl << "ts" << m_lastNavReasonTs;
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
      recordNavReason(QStringLiteral("back_dedao_detail"), QUrl(detail));
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
      recordNavReason(QStringLiteral("back_fallback_home"),
                      QUrl(QStringLiteral("https://weread.qq.com/")));
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
    recordNavReason(QStringLiteral("gesture_go_home"), home);
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
    if (SmartRefreshManager *mgr = smartRefreshForPage()) {
      mgr->triggerMenu();
    }
    QTimer::singleShot(5000, this, [this]() { captureFrame(); });
    QTimer::singleShot(6000, this, [this]() { captureFrame(); });
    runDomDiagnostic();
  }


bool WereadBrowser::handleMenuTap(const QPointF &globalPos) {
    if (!m_menu || !m_menu->isVisible())
      return false;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QPointF delta = globalPos - m_lastMenuTapPos;
    const qreal dist2 = delta.x() * delta.x() + delta.y() * delta.y();
    if (m_lastMenuTapMs > 0 && (nowMs - m_lastMenuTapMs) < 50 &&
        dist2 <= (24.0 * 24.0)) {
      qInfo() << "[MENU] debounce tap" << (nowMs - m_lastMenuTapMs) << "ms"
              << "pos" << globalPos;
      return true;
    }
    m_lastMenuTapMs = nowMs;
    m_lastMenuTapPos = globalPos;
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
    const bool isWeReadBookPage = isWeReadBook();
    const bool isDedaoBookPage = isDedaoBook();
    const bool isBook = isWeReadBookPage || isDedaoBookPage;
    QByteArray b(1, isBook ? '\x01' : '\x00');
    m_stateSender.writeDatagram(b, QHostAddress(QStringLiteral("127.0.0.1")),
                                45456);
    qInfo() << "[STATE] isBookPage" << isBook << "url" << currentUrl;
    const bool wasBook = m_prevIsBookPage;
    m_prevIsBookPage = isBook;
    m_isBookPage = isBook;
    // 非书籍页：交互优先；书籍页：阅读优先（按站点拆分）
    auto applyPolicy = [](SmartRefreshManager *mgr, bool book) {
      if (!mgr)
        return;
      mgr->setBookPage(book);
      const auto targetPolicy =
          book ? SmartRefreshManager::PolicyReadingFirst
               : SmartRefreshManager::PolicyInteractionFirst;
      if (mgr->policy() != targetPolicy) {
        mgr->setPolicy(targetPolicy);
      }
    };
    applyPolicy(m_smartRefreshWeRead, isWeReadBookPage);
    applyPolicy(m_smartRefreshDedao, isDedaoBookPage);
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
      m_dedaoStyleLogged = false;
      m_dedaoCatalogScanning = false;
      m_dedaoCatalogScanRounds = 0;
      m_dedaoCatalogScanStepPx = 0;
      m_dedaoCatalogMaxScrollHeight = 0;
      m_dedaoCatalogStableScrollHeightRounds = 0;
      m_dedaoCatalogScanKey.clear();
      m_dedaoCatalogJumpPending = false;
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


bool WereadBrowser::isDedaoSite() const {
    const QString u = currentUrl.toString();
    return u.contains(QStringLiteral("dedao.cn"));
  }


QWebEngineView *WereadBrowser::getView() const { return m_view; }



SmartRefreshManager *WereadBrowser::getSmartRefreshWeRead() const {
    return m_smartRefreshWeRead;
  }

SmartRefreshManager *WereadBrowser::getSmartRefreshDedao() const {
    return m_smartRefreshDedao;
  }

SmartRefreshManager *WereadBrowser::smartRefreshForUrl(const QUrl &url) const {
    const QString u = url.toString();
    if (u.contains(QStringLiteral("dedao.cn"))) {
      return m_smartRefreshDedao;
    }
    if (u.contains(QStringLiteral("weread.qq.com"))) {
      return m_smartRefreshWeRead;
    }
    return nullptr;
  }

SmartRefreshManager *WereadBrowser::smartRefreshForPage() const {
    QUrl pageUrl;
    if (m_view && m_view->page()) {
      pageUrl = m_view->page()->url();
      if (pageUrl.isEmpty()) {
        pageUrl = m_view->url();
      }
    }
    if (pageUrl.isEmpty()) {
      pageUrl = currentUrl;
    }
    return smartRefreshForUrl(pageUrl);
  }

SmartRefreshManager *WereadBrowser::getSmartRefresh() const {
    return smartRefreshForPage();
  }

void WereadBrowser::handleSmartRefreshEvents(const QString &json) {
    SmartRefreshManager *mgr = smartRefreshForPage();
    if (!mgr) {
      qInfo() << "[SMART_REFRESH] drop events (no manager) url"
              << (m_view ? m_view->url() : currentUrl);
      return;
    }
    mgr->parseJsEvents(json);
  }

void WereadBrowser::handleSmartRefreshBurstEnd() {
    SmartRefreshManager *mgr = smartRefreshForPage();
    if (!mgr) {
      qInfo() << "[SMART_REFRESH] drop burst end (no manager) url"
              << (m_view ? m_view->url() : currentUrl);
      return;
    }
    mgr->triggerBurstEnd();
  }



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
