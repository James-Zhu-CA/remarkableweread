#include "weread_browser.h"
#include <QTextStream>

void WereadBrowser::keepAliveNearChapterEndWithRetry() {
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
        "    const ok = hasBook; // 不再强依赖 idx/total"
        "    const reason = hasBook ? (hasIdx ? 'ok' : 'missing_idx') : "
        "'missing_book';"
        "    return {ok, reason, source, bookId, chapterUid: uid, idx, total, "
        "hasLocal, localIdx, shouldPing:false};"
        "  } catch(e) { return {ok:false, error:''+e}; }"
        "})();");
    m_view->page()->runJavaScript(js, [this](const QVariant &res) {
      const QVariantMap map = res.toMap();
      const bool ok = map.value(QStringLiteral("ok")).toBool();
      const int total = map.value(QStringLiteral("total")).toInt();
      const QString bookId = map.value(QStringLiteral("bookId")).toString();

      // 检测书籍内容加载失败
      const bool isBookPage = !bookId.isEmpty();
      const bool contentFailed = isBookPage && (!ok || total == 0);

      if (contentFailed) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();

        // 二次确认机制：第一次检查失败后，延迟再次检查，避免误判
        if (!m_contentFirstCheckFailed) {
          // 第一次检查失败，标记并延迟进行二次确认
          m_contentFirstCheckFailed = true;
          qInfo() << "[RETRY] First check failed, scheduling confirmation "
                     "check after"
                  << CONTENT_CONFIRM_DELAY_MS << "ms. BookId:" << bookId
                  << "ok:" << ok << "total:" << total;
          QTimer::singleShot(CONTENT_CONFIRM_DELAY_MS, this, [this]() {
            checkContentAndRetryIfNeeded(); // 二次确认检查
          });
          return; // 不立即重试，等待二次确认
        }

        // 二次确认也失败，才真正触发重试
        m_contentFirstCheckFailed = false; // 重置标志

        // 检查重试间隔：如果最近重试过，跳过（避免频繁重试）
        if (now - m_lastContentRetryTime < CONTENT_RETRY_INTERVAL_MS) {
          qInfo() << "[RETRY] Skipping retry (interval, last retry was"
                  << (now - m_lastContentRetryTime) << "ms ago)";
          return;
        }

        // 检查最大重试次数
        if (m_contentRetryCount >= MAX_CONTENT_RETRIES) {
          qWarning() << "[RETRY] Max retries reached (" << MAX_CONTENT_RETRIES
                     << "), giving up. BookId:" << bookId;
          return;
        }

        m_contentRetryCount++;
        m_lastContentRetryTime = now;

        qInfo()
            << "[RETRY] Content load failed (confirmed), retrying via JS API ("
            << m_contentRetryCount << "/" << MAX_CONTENT_RETRIES
            << ") BookId:" << bookId << "ok:" << ok << "total:" << total;

        // 触发重试：通过JavaScript重新触发API请求（不重新加载页面）
        if (m_view && m_view->page()) {
          // 直接调用重试函数，不等待Promise结果（因为Qt的runJavaScript不能直接处理Promise）
          const QString retryJs = QStringLiteral(
              "(function() {"
              "  try {"
              "    if (window.__WR_RETRY_BOOK_READ__) {"
              "      window.__WR_RETRY_BOOK_READ__().then(() => {"
              "        console.log('[RETRY] JS API retry succeeded');"
              "      }).catch(err => {"
              "        console.error('[RETRY] JS API retry failed:', err);"
              "      });"
              "      return {ok: true, method: 'js_api_retry_triggered'};"
              "    } else {"
              "      return {ok: false, error: 'retry_function_not_available'};"
              "    }"
              "  } catch(e) {"
              "    return {ok: false, error: String(e)};"
              "  }"
              "})()");

          m_view->page()->runJavaScript(retryJs, [this,
                                                  bookId](const QVariant &res) {
            QVariantMap map = res.toMap();
            bool jsOk = map.value(QStringLiteral("ok")).toBool();
            if (jsOk) {
              qInfo() << "[RETRY] JavaScript API retry triggered for BookId:"
                      << bookId;
            } else {
              QString error = map.value(QStringLiteral("error")).toString();
              qWarning() << "[RETRY] JavaScript API retry function not "
                            "available for BookId:"
                         << bookId << "error:" << error;
              // 如果JS重试函数不可用，降级为页面重载（仅作为最后手段）
              if (m_enableAutoReload &&
                  m_contentRetryCount >= MAX_CONTENT_RETRIES) {
                qInfo() << "[RETRY] Falling back to page reload (last resort)";
                if (m_view) {
                  m_view->reload();
                }
              }
            }
          });
        }
      } else if (ok && total > 0) {
        // 内容加载成功，重置所有状态
        if (m_contentRetryCount > 0) {
          qInfo() << "[RETRY] Content loaded successfully after"
                  << m_contentRetryCount << "retries";
          m_contentRetryCount = 0;
        }
        m_contentFirstCheckFailed = false; // 重置第一次检查失败标志
      }

      // 原有的日志记录
      qInfo() << "[KEEPALIVE]" << res;
    });
  }


void WereadBrowser::onPageTurnEvent() {
    if (m_pingDisabled || m_pingInterval <= 0)
      return;
    m_pageTurnCounter++;
    if ((m_pageTurnCounter % m_pingInterval) == 0) {
      sendKeepAlivePing(m_pageTurnCounter, 1);
    }
  }


void WereadBrowser::sendKeepAlivePing(int triggerCount, int attempt) {
    if (m_pingDisabled || m_pingInterval <= 0)
      return;
    if (attempt > m_pingMaxRetries) {
      qWarning() << "[KEEPALIVE_PING]"
                 << "count" << triggerCount << "giveup after attempts"
                 << m_pingMaxRetries;
      maybeReloadAfterPingFail(triggerCount);
      return;
    }
    qInfo() << "[KEEPALIVE_PING_SCHED]"
            << "count" << triggerCount << "attempt" << attempt << "dispatch";
    // TCP 探测：仅连接 weread.qq.com:80，不依赖页面/SSL，超时强制结束
    QTcpSocket *sock = new QTcpSocket(this);
    sock->setProperty("handled", false);
    QTimer *timeout = new QTimer(sock);
    timeout->setSingleShot(true);
    timeout->setInterval(m_pingTimeoutMs);

    auto finalize = [this, sock, triggerCount, attempt](bool ok,
                                                        const QString &reason) {
      if (sock->property("handled").toBool())
        return;
      sock->setProperty("handled", true);
      if (ok) {
        qInfo() << "[KEEPALIVE_PING]"
                << "count" << triggerCount << "attempt" << attempt << "ok"
                << "status" << 200 << reason;
      } else {
        qWarning() << "[KEEPALIVE_PING]"
                   << "count" << triggerCount << "attempt" << attempt << "fail"
                   << reason << "status" << 0;
        if (attempt < m_pingMaxRetries) {
          QTimer::singleShot(m_pingRetryDelayMs, this,
                             [this, triggerCount, attempt]() {
                               sendKeepAlivePing(triggerCount, attempt + 1);
                             });
        } else {
          maybeReloadAfterPingFail(triggerCount);
        }
      }
      if (sock->isOpen()) {
        sock->disconnectFromHost();
      }
      sock->deleteLater();
    };

    connect(sock, &QTcpSocket::connected, this,
            [finalize]() { finalize(true, QStringLiteral("tcp_connect_ok")); });
    connect(sock, &QTcpSocket::errorOccurred, this,
            [finalize, sock](QAbstractSocket::SocketError) {
              finalize(false, sock->errorString());
            });
    connect(timeout, &QTimer::timeout, this, [sock, finalize]() {
      sock->abort();
      finalize(false, QStringLiteral("timeout"));
    });

    timeout->start();
    sock->connectToHost(QStringLiteral("weread.qq.com"), 80);
  }


void WereadBrowser::maybeReloadAfterPingFail(int triggerCount) {
    // 可通过 WEREAD_PING_RELOAD=1 启用；默认不触发 reload
    if (!qEnvironmentVariableIsSet("WEREAD_PING_RELOAD") ||
        qEnvironmentVariableIntValue("WEREAD_PING_RELOAD") == 0) {
      return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // 冷却 120s 避免频繁 reload
    if (now - m_pingLastReloadMs < 120000) {
      qWarning() << "[PING_RELOAD] skipped (cooldown) count" << triggerCount;
      return;
    }
    m_pingLastReloadMs = now;
    qWarning() << "[PING_RELOAD] trigger Reload after ping fail, count"
               << triggerCount;
    if (m_view) {
      m_view->reload();
    }
  }


void WereadBrowser::runDomDiagnostic() {
    if (!m_view || !m_view->page())
      return;
    const QString js = QStringLiteral(
        "(() => {"
        "  const report={url:location.href};"
        "  const catSelectors=['.readerControls "
        ".catalog','.readerControls_item.catalog','.catalog','.readerCatalog','"
        "button[title*=\"\\u76ee\\u5f55\"]','[data-action=\"catalog\"]'];"
        "  const catFound=catSelectors.map(sel=>{const "
        "el=document.querySelector(sel);return "
        "el?{sel,visible:getComputedStyle(el).display!=='none',cls:el."
        "className,tag:el.tagName}:null;}).filter(Boolean);"
        "  const "
        "catText=[];document.querySelectorAll('button,div[role=\"button\"],a,"
        "span').forEach(el=>{const "
        "t=(el.textContent||'').trim();if(t.includes('\\u76ee\\u5f55'))catText."
        "push({tag:el.tagName,cls:el.className,visible:getComputedStyle(el)."
        "display!=='none'});});"
        "  const panel=document.querySelector('.readerCatalog');"
        "  report.catalog={found:catFound, byText:catText.slice(0,5), "
        "panelVisible: !!panel && getComputedStyle(panel).display!=='none'};"
        "  const "
        "dots=Array.from(document.querySelectorAll('.reader_font_control_"
        "slider_track_level_dot_container "
        ".reader_font_control_slider_track_level_dot'));"
        "  report.font={total:dots.length, "
        "dots:dots.slice(0,7).map(d=>({cls:d.className,active:/"
        "(show|active|current)/.test(d.className)})), panelVisible: "
        "!!document.querySelector('.readerControls') && "
        "getComputedStyle(document.querySelector('.readerControls')).display!=="
        "'none', bodyClass:document.body.className};"
        "  const "
        "themeBtns=Array.from(document.querySelectorAll('.readerControls "
        ".dark, .readerControls .white, .readerControls_item.dark, "
        ".readerControls_item.white'));"
        "  report.theme={count:themeBtns.length, "
        "buttons:themeBtns.slice(0,4).map(b=>({cls:b.className,vis:"
        "getComputedStyle(b).display!=='none'})), dataTheme: "
        "document.body.getAttribute('data-theme')||document.documentElement."
        "getAttribute('data-theme')||'', bodyClass:document.body.className, "
        "htmlClass:document.documentElement.className};"
        "  const "
        "controls=Array.from(document.querySelectorAll('.readerControls > "
        "*')).slice(0,10).map(el=>({tag:el.tagName,cls:el.className,text:(el."
        "textContent||'').trim().slice(0,30)}));"
        "  report.controls=controls;"
        "  return report;"
        "})();");
    m_view->page()->runJavaScript(js, [](const QVariant &res) {
      const auto map = res.toMap();
      qInfo() << "[DIAG] url" << map.value(QStringLiteral("url")).toString();
      const auto catalog = map.value(QStringLiteral("catalog")).toMap();
      qInfo() << "[DIAG] catalog found"
              << catalog.value(QStringLiteral("found")).toList().size()
              << "byText"
              << catalog.value(QStringLiteral("byText")).toList().size()
              << "panelVisible"
              << catalog.value(QStringLiteral("panelVisible")).toBool();
      const auto font = map.value(QStringLiteral("font")).toMap();
      qInfo() << "[DIAG] font dots"
              << font.value(QStringLiteral("total")).toInt() << "panelVisible"
              << font.value(QStringLiteral("panelVisible")).toBool()
              << "bodyClass"
              << font.value(QStringLiteral("bodyClass")).toString();
      const auto theme = map.value(QStringLiteral("theme")).toMap();
      qInfo() << "[DIAG] theme buttons"
              << theme.value(QStringLiteral("count")).toInt() << "dataTheme"
              << theme.value(QStringLiteral("dataTheme")).toString()
              << "bodyClass"
              << theme.value(QStringLiteral("bodyClass")).toString()
              << "htmlClass"
              << theme.value(QStringLiteral("htmlClass")).toString();
      const auto controls = map.value(QStringLiteral("controls")).toList();
      QStringList ctrlSumm;
      for (const auto &c : controls) {
        const auto m = c.toMap();
        ctrlSumm << (m.value(QStringLiteral("tag")).toString() + ":" +
                     m.value(QStringLiteral("cls")).toString());
      }
      qInfo() << "[DIAG] readerControls children" << ctrlSumm.join(" | ");
    });
  }


void WereadBrowser::runDedaoProbe() {
    if (!m_view || !m_view->page())
      return;
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
    m_view->page()->runJavaScript(
        js, [](const QVariant &res) { qInfo() << "[DEDAO_PROBE]" << res; });
  }
