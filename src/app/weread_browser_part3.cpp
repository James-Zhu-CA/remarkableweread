#include "weread_browser.h"
#include <QTextStream>

void WereadBrowser::noteDomEventFromJson(const QString &json) {
  if (m_pendingInputFallbackSeq <= 0 && !logLevelAtLeast(LogLevel::Info)) {
    return;
  }
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isArray())
    return;
  const QJsonArray arr = doc.array();
  int totalScore = 0;
  bool hasDom = false;
  for (const QJsonValue &val : arr) {
    if (!val.isObject())
      continue;
    const QJsonObject obj = val.toObject();
    if (obj.value(QStringLiteral("t")).toString() != QStringLiteral("dom"))
      continue;
    hasDom = true;
    totalScore += obj.value(QStringLiteral("s")).toInt();
  }
  if (!hasDom)
    return;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  m_lastDomEventMs = now;
  m_lastDomEventScore = totalScore;
  if (m_pendingInputFallbackSeq > 0 && now >= m_inputFallbackStartMs) {
    const int seq = m_pendingInputFallbackSeq;
    m_inputDomObserved = true;
    logPageTurnEffect(seq, QStringLiteral("input"), QString(), totalScore);
    m_pendingInputFallbackSeq = 0;
  }
}

void WereadBrowser::updateUserAgentForUrl(const QUrl &url) {
  if (!m_profile)
    return;
  const QString u = url.toString();
  const bool weReadBook = u.contains(QStringLiteral("weread.qq.com")) &&
                          u.contains(QStringLiteral("/web/reader/"));
  const bool dedaoBook = u.contains(QStringLiteral("dedao.cn")) &&
                         u.contains(QStringLiteral("/ebook/reader"));
  // 检测是否是微信读书首页（weread.qq.com但不是书籍页）
  const bool weReadHome =
      u.contains(QStringLiteral("weread.qq.com")) && !weReadBook;
  QString targetUA;
  if (dedaoBook) {
    targetUA = m_kindleUA.isEmpty() ? m_uaNonWeRead : m_kindleUA;
  } else if (weReadBook) {
    targetUA = m_uaWeReadBook;
  } else if (weReadHome) {
    // 微信读书首页：使用非书籍UA（当前为 Kindle）
    targetUA = m_uaNonWeRead;
    if (m_currentUA != targetUA) {
      qInfo() << "[UA] weread home: switching to non-book UA from"
              << m_currentUA << "to" << targetUA;
    }
  } else {
    targetUA = m_uaNonWeRead;
  }
  if (targetUA == m_currentUA) {
    // UA相同，无需切换
    m_uaMode = detectUaMode(m_currentUA);
    return;
  }
  m_profile->setHttpUserAgent(targetUA);
  m_currentUA = targetUA;
  m_uaMode = detectUaMode(m_currentUA);
  qInfo() << "[UA] switched"
          << (weReadBook ? "weread-book"
                         : (weReadHome ? "weread-home" : "non-weread"))
          << targetUA;
}

void WereadBrowser::ensureDedaoDefaults() {
  if (!isDedaoBook() || !m_view || !m_view->page())
    return;
  // 强制 Kindle UA（需求：进入得到书籍页默认 UA 为 Kindle）
  if (!m_kindleUA.isEmpty() && m_profile &&
      m_profile->httpUserAgent() != m_kindleUA) {
    m_profile->setHttpUserAgent(m_kindleUA);
    m_currentUA = m_kindleUA;
    m_uaMode = detectUaMode(m_currentUA);
    qInfo() << "[DEDAO] UA forced to kindle";
  }
  // 缩放：2.0
  const qreal targetZoom = 2.0;
  if (!qFuzzyCompare(m_view->zoomFactor(), targetZoom)) {
    m_view->setZoomFactor(targetZoom);
    qInfo() << "[DEDAO] zoom set" << targetZoom;
  }
}

void WereadBrowser::scheduleCapture(int delayMs) {
  Q_UNUSED(delayMs); /* no-op in integrated mode */
}

void WereadBrowser::alignWeReadPagination() {
  if (!isWeReadBook() || m_weReadBookMode != QStringLiteral("mobile") ||
      !m_view || !m_view->page())
    return;
  const QString js = QStringLiteral(
      "(() => {"
      "  const el = document.querySelector('.renderTargetContent') || "
      "document.scrollingElement || document.documentElement;"
      "  if (!el) return {ok:false, reason:'no-container'};"
      "  try {"
      "    // el.style.padding = '20px 35px'; // 临时禁用：排查布局/隐藏问题"
      "    el.style.boxSizing = 'border-box';"
      "    el.style.width = '100%';"
      "    el.style.maxWidth = '100%';"
      "  } catch(e) {}"
      "  const sample = el.querySelector('p, div, span');"
      "  const lh = sample ? parseFloat(getComputedStyle(sample).lineHeight) "
      "|| 32 : 32;"
      "  const padTop = parseFloat(getComputedStyle(el).paddingTop) || 0;"
      "  const cur = el.scrollTop;"
      "  const aligned = Math.max(0, Math.round((cur - padTop) / lh) * lh + "
      "padTop);"
      "  if (Math.abs(aligned - cur) > 0.5) el.scrollTo({ top: aligned, "
      "behavior: 'auto' });"
      "  return {ok:true, from:cur, to:aligned, lineHeight:lh};"
      "})()");
  m_view->page()->runJavaScript(js, [](const QVariant &res) {
    qInfo() << "[PAGER] align weRead" << res;
  });
}

void WereadBrowser::runVisualDiagnostic() {
  if (!m_view || !m_view->page())
    return;
  const QString js = QStringLiteral(
      "(() => {"
      "  try {"
      "    const target = document.querySelector('.renderTargetContent') || "
      "document.querySelector('.readerChapterContent') || document.body;"
      "    if (!target) return {error: 'target-not-found'};"
      "    const style = window.getComputedStyle(target);"
      "    const rect = target.getBoundingClientRect();"
      "    const mask = document.querySelector('.loading, .mask, "
      ".reader_loading');"
      "    let maskInfo = 'none';"
      "    if (mask) {"
      "      const ms = window.getComputedStyle(mask);"
      "      if (ms.display !== 'none' && ms.visibility !== 'hidden' && "
      "ms.opacity !== '0') {"
      "        maskInfo = {cls: mask.className, zIndex: ms.zIndex, bg: "
      "ms.backgroundColor, disp: ms.display, vis: ms.visibility, op: "
      "ms.opacity};"
      "      }"
      "    }"
      "    const iframeInfo = (()=>{"
      "      const frames = Array.from(document.querySelectorAll('iframe'));"
      "      const samples = [];"
      "      frames.forEach((f,idx)=>{"
      "        if (samples.length>=3) return;"
      "        const r = f.getBoundingClientRect();"
      "        const entry = {idx, src:(f.src||'').slice(0,120), "
      "rect:{x:r.x,y:r.y,w:r.width,h:r.height}};"
      "        try {"
      "          const d = f.contentDocument || (f.contentWindow && "
      "f.contentWindow.document);"
      "          if (d && d.body && d.body.innerText) entry.textLen = "
      "d.body.innerText.length;"
      "        } catch(_) { entry.blocked = true; }"
      "        samples.push(entry);"
      "      });"
      "      return {count:frames.length, samples};"
      "    })();"
      "    return {"
      "      tag: target.tagName,"
      "      cls: target.className,"
      "      rect: {x: rect.x, y: rect.y, w: rect.width, h: rect.height},"
      "      style: {display: style.display, visibility: style.visibility, "
      "opacity: style.opacity, zIndex: style.zIndex, position: "
      "style.position, color: style.color, bgColor: style.backgroundColor, "
      "font: style.fontFamily},"
      "      mask: maskInfo,"
      "      textLen: (target.innerText||'').length,"
      "      iframe: iframeInfo"
      "    };"
      "  } catch(e) { return {error: String(e)}; }"
      "})()");
  m_view->page()->runJavaScript(js, [](const QVariant &res) {
    qWarning().noquote() << "[VISUAL_DIAG]" << res;
  });
}

void WereadBrowser::applyBookPageFixes() {
  if (!isWeReadBook())
    return;
  if (!m_view || !m_view->page())
    return;

  // 延迟执行，等待页面内容加载
  QTimer::singleShot(500, this, [this]() {
    // 检查是否有文本内容
    m_view->page()->runJavaScript(
        QStringLiteral(
            "(() => {"
            "  try {"
            "    const bodyLen = (document.body && document.body.innerText) "
            "? document.body.innerText.length : 0;"
            "    const cont = document.querySelector('.renderTargetContent');"
            "    const contLen = cont ? ((cont.innerText||'').length) : 0;"
            "    let iframeText = 0;"
            "    const frames = "
            "Array.from(document.querySelectorAll('iframe'));"
            "    frames.forEach(f=>{"
            "      try{ const "
            "d=f.contentDocument||f.contentWindow?.document; "
            "if(d&&d.body&&d.body.innerText){ iframeText += "
            "d.body.innerText.length; } }catch(_e){}"
            "    });"
            "    const hasText = (contLen > 100) || (bodyLen > 500) || "
            "(iframeText > 300);"
            "    return {hasText, bodyLen, contLen, iframeText};"
            "  } catch(e){ return {error:String(e)}; }"
            "})()"),
        [this](const QVariant &res) {
          const auto m = res.toMap();
          const bool hasText = m.value(QStringLiteral("hasText")).toBool();

          // 触发 CONTENT_READY 高优先级事件（不受递增阈值限制）
          if (hasText && !m_contentReadyTriggered) {
            m_contentReadyTriggered = true;
            if (m_smartRefresh) {
              m_smartRefresh->triggerContentReady();
              qInfo() << "[CONTENT_READY] Content ready detected, triggering "
                         "refresh"
                      << "bodyLen="
                      << m.value(QStringLiteral("bodyLen")).toInt()
                      << "contLen="
                      << m.value(QStringLiteral("contLen")).toInt()
                      << "iframeText="
                      << m.value(QStringLiteral("iframeText")).toInt();
            }
          }
          if (hasText) {
            applyWeReadFontOverrideIfNeeded();
          }

          // 应用 iframe 修复样式
          if (hasText && !m_iframeFixApplied) {
            m_iframeFixApplied = true;
            const QString fixJs = QStringLiteral(
                "(() => {"
                "  try {"
                "    const cont = "
                "document.querySelector('.renderTargetContent');"
                "    if (cont) {"
                "      cont.style.minHeight = '100vh';"
                "      cont.style.width = '100%';"
                "      cont.style.display = 'block';"
                "      cont.style.opacity = '1';"
                "      cont.style.visibility = 'visible';"
                "      cont.style.background = '#fff';"
                "    }"
                "    const iframes = "
                "Array.from(document.querySelectorAll('iframe'));"
                "    iframes.forEach(f => {"
                "      f.style.display = 'block';"
                "      f.style.minHeight = '100vh';"
                "      f.style.width = '100%';"
                "      try {"
                "        const d = f.contentDocument || (f.contentWindow && "
                "f.contentWindow.document);"
                "        if (d && d.body) {"
                "          d.body.style.background = '#fff';"
                "          d.body.style.color = '#000';"
                "          d.body.style.opacity = '1';"
                "          d.documentElement && "
                "(d.documentElement.style.background = '#fff');"
                "        }"
                "      } catch(_) {}"
                "    });"
                "    window.scrollBy(0,1); "
                "setTimeout(()=>window.scrollBy(0,-1),30);"
                "    return {ok:true, iframes: iframes.length};"
                "  } catch(e){ return {ok:false, error:String(e)};}"
                "})()");
            m_view->page()->runJavaScript(fixJs, [](const QVariant &res) {
              qInfo() << "[STYLE] iframe fix" << res;
            });
          }

          // 注入低功耗样式（仅一次）
          if (hasText && !m_lowPowerCssInjected) {
            m_lowPowerCssInjected = true;
            const QString lowPowerCss = QStringLiteral(
                "(() => {"
                "  try {"
                "    if (document.getElementById('weread-low-power-style')) "
                "return {ok:true, existed:true};"
                "    const style = document.createElement('style');"
                "    style.id = 'weread-low-power-style';"
                "    style.textContent = `"
                "      * {"
                "        animation-duration: 0.001s !important;"
                "        animation-iteration-count: 1 !important;"
                "        transition-duration: 0s !important;"
                "        scroll-behavior: auto !important;"
                "      }"
                "      html, body, .readerChapterContent, "
                ".renderTargetContent, .readerContent {"
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
                "})()");
            m_view->page()->runJavaScript(lowPowerCss, [](const QVariant &res) {
              qInfo() << "[STYLE] low-power css" << res;
            });
          }
        });
  });
}

void WereadBrowser::applyWeReadFontOverrideIfNeeded() {
  if (!isWeReadBook())
    return;
  if (!m_view || !m_view->page())
    return;
  const QString js = QStringLiteral(R"WR(
(() => {
  try {
    const key = 'wrLocalSetting';
    let setting = {};
    try { setting = JSON.parse(localStorage.getItem(key) || '{}'); } catch(e) { setting = {}; }
    const font = (setting.fontFamily || '').trim();
    if (!font) return {ok:false, reason:'no-font-setting'};
    if (font.toLowerCase() === 'custom_noto_sans') return {ok:false, reason:'custom-font'};
    const cont = document.querySelector('.renderTargetContent') || document.body;
    if (!cont) return {ok:false, reason:'no-container'};
    const computed = window.getComputedStyle ? getComputedStyle(cont).fontFamily : '';
    const norm = (s) => (s || '').toLowerCase().replace(/[^a-z0-9]/g, '');
    const normFont = norm(font);
    const normComp = norm(computed);
    const already =
      (font && computed.toLowerCase().includes(font.toLowerCase())) ||
      (normFont && normComp.includes(normFont)) ||
      normComp.includes('tsangeryunheiw05');
    if (already) return {ok:true, applied:false, font, computed};
    const cssId = 'wr-font-setting-override';
    let style = document.getElementById(cssId);
    if (!style) { style = document.createElement('style'); style.id = cssId; }
    const fontCss = JSON.stringify(font);
    style.textContent =
      '.renderTargetContent, .renderTargetContent * { font-family: ' +
      fontCss + ", 'TsangerYunHei-W05', 'PingFang SC', 'Microsoft YaHei', sans-serif !important; }";
    const root = document.head || document.documentElement || document.body;
    if (root && style.parentNode !== root) root.appendChild(style);
    try { cont.offsetHeight; } catch(_e) {}
    if (document.fonts && document.fonts.ready && document.fonts.ready.then) {
      document.fonts.ready.then(() => { try { cont.offsetHeight; } catch(_e) {} });
    }
    return {ok:true, applied:true, font, computed};
  } catch(e) { return {ok:false, error:String(e)}; }
})()
)WR");
  m_view->page()->runJavaScript(
      js, [](const QVariant &res) { qInfo() << "[FONT_OVERRIDE]" << res; });
}

void WereadBrowser::handleJsUnexpected(const QString &message,
                                       const QString &source) {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - m_jsLastUnexpectedMs > 10000) {
    m_jsUnexpectedCount = 0;
  }
  m_jsUnexpectedCount++;
  m_jsLastUnexpectedMs = now;
  const bool allowLog =
      (m_jsUnexpectedCount <= 2) || (now - m_jsUnexpectedLastLogMs > 10000);
  if (allowLog) {
    qWarning() << "[JS_UNEXPECTED]" << message << "source" << source << "count"
               << m_jsUnexpectedCount;
    m_jsUnexpectedLastLogMs = now;
  }
  const bool isUnexpectedEnd = message.contains(
      QStringLiteral("Unexpected end of input"), Qt::CaseInsensitive);
  if (!isUnexpectedEnd) {
    if (allowLog) {
      qWarning()
          << "[JS_UNEXPECTED] reload disabled (non-unexpected syntax error)";
    }
    return;
  }
  const bool allowReload =
      qEnvironmentVariableIsSet("WEREAD_JS_RELOAD_ON_ERROR") &&
      qEnvironmentVariableIntValue("WEREAD_JS_RELOAD_ON_ERROR") != 0;
  if (!allowReload) {
    if (allowLog) {
      qWarning() << "[JS_UNEXPECTED] reload disabled";
    }
    return;
  }
  if (m_jsUnexpectedCount < 2) {
    if (allowLog) {
      qWarning() << "[JS_UNEXPECTED] reload skipped (wait for repeat)";
    }
    return;
  }
  if (now - m_jsReloadCooldownMs < 120000) {
    if (allowLog) {
      qWarning() << "[JS_UNEXPECTED] reload skipped (cooldown)";
    }
    return;
  }
  if (m_view && m_view->page()) {
    qWarning() << "[JS_UNEXPECTED] triggering ReloadAndBypassCache";
    m_jsReloadCooldownMs = now;
    m_view->page()->triggerAction(QWebEnginePage::ReloadAndBypassCache);
  }
}

void WereadBrowser::updateHeartbeat() {
  Q_UNUSED(m_isBookPage);
  // heartbeat captures disabled; keep stub to avoid timer reuse
  m_heartbeatTimer.stop();
}

void WereadBrowser::cancelPendingCaptures() { /* no-op */
}

void WereadBrowser::checkContentAndRetryIfNeeded() {
  if (!m_view || !m_view->page())
    return;

  const qint64 now = QDateTime::currentMSecsSinceEpoch();

  // 冷却期检查：如果最近检查过，跳过（避免频繁检查）
  // 但如果这是二次确认检查（m_contentFirstCheckFailed == true），则跳过冷却期
  if (!m_contentFirstCheckFailed &&
      now - m_lastContentCheckTime < CONTENT_CHECK_COOLDOWN_MS) {
    qInfo() << "[RETRY] Skipping check (cooldown, last check was"
            << (now - m_lastContentCheckTime) << "ms ago)";
    return;
  }

  m_lastContentCheckTime = now;

  // 调用 keepAliveNearChapterEnd 检查内容状态（带重试逻辑）
  keepAliveNearChapterEndWithRetry();
}

void WereadBrowser::keepAliveNearChapterEnd() {
  if (!m_view || !m_view->page())
    return;
  const QString js = QStringLiteral(
      "(() => {"
      "  try {"
      "    const obs = window.__WR_CHAPTER_OBS || {};"
      "    let source = '';"
      "    let bookId = '';"
      "    let idx = -1;"
      "    let total = 0;"
      "    let uid = null;"
      "    if (obs && (obs.bookId || obs.chapterUid || obs.total || "
      "obs.chapterIdx!==undefined || obs.idx!==undefined || "
      "obs.localIdx!==undefined)) {"
      "      source = 'observer';"
      "      bookId = obs.bookId || '';"
      "      uid = obs.chapterUid || null;"
      "      if (obs.total) total = obs.total;"
      "      if (obs.chapterIdx !== undefined) idx = obs.chapterIdx;"
      "      if (idx < 0 && obs.idx !== undefined) idx = obs.idx;"
      "      if (idx < 0 && obs.localIdx !== undefined) idx = obs.localIdx;"
      "    }"
      "    const urlMatch = (location.href || "
      "'').match(/reader\\/([a-zA-Z0-9]+)/);"
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
      "    const hasBook = !!bookId;"
      "    const hasIdx = (idx >= 0 && total > 0);"
      "    const ok = hasBook; // 不再强依赖 "
      "idx/total，可部分信息缺失时仍返回 ok=true"
      "    const reason = hasBook ? (hasIdx ? 'ok' : 'missing_idx') : "
      "'missing_book';"
      "    return {ok, reason, source, bookId, chapterUid: uid, idx, total, "
      "hasLocal, localIdx, shouldPing:false};"
      "  } catch(e) { return {ok:false, error:''+e}; }"
      "})();");
  m_view->page()->runJavaScript(js, [this](const QVariant &res) {
    const QVariantMap map = res.toMap();
    const bool ok = map.value(QStringLiteral("ok")).toBool();
    if (!ok && map.value(QStringLiteral("reason")).toString() ==
                   QLatin1String("no_state")) {
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
