#include "weread_browser.h"
#include <QTextStream>

namespace {
const QString &chapterObserverScriptSource() {
  static const QString js = QStringLiteral(R"WR(
(() => {
  const host = location.host || '';
  const path = location.pathname || '';
  const isWeRead = /weread\.qq\.com$/i.test(host);
  const isReader = /\/web\/reader\//i.test(path);
  if (!isWeRead || !isReader) {
    return {ok:false, reason:'not-reader', host, path};
  }
  window.__WR_CHAPTER_OBS = window.__WR_CHAPTER_OBS || {};
  const obs = window.__WR_CHAPTER_OBS;
  let installed = false;

  function updateReaderId() {
    try {
      const m = (location.pathname || '').match(/\/web\/reader\/([^/?#]+)/);
      if (m) {
        const next = m[1];
        if (obs.readerId !== next) {
          obs.readerId = next;
          try {
            console.log('[OBS] readerId', next, 'href', location.href);
          } catch (e) {}
        }
      }
    } catch (e) {}
  }

  function installNavLogger() {
    try {
      if (window.__WR_NAV_LOG_INSTALLED__) return;
      window.__WR_NAV_LOG_INSTALLED__ = true;
      const logNav = (tag, url) => {
        try {
          console.log('[NAV_STATE]', tag, String(url || ''),
                      'href', location.href, 'search', location.search || '');
        } catch (e) {}
      };
      const wrap = (fn, tag) => {
        return function(state, title, url) {
          logNav(tag, url);
          return fn.apply(this, arguments);
        };
      };
      if (history && history.pushState) {
        history.pushState = wrap(history.pushState, 'pushState');
      }
      if (history && history.replaceState) {
        history.replaceState = wrap(history.replaceState, 'replaceState');
      }
      window.addEventListener('popstate', () => logNav('popstate', ''));
      window.addEventListener('hashchange', () => logNav('hashchange', ''));
      logNav('init', '');
    } catch (e) {}
  }

  function updateLocalIdx() {
    try {
      const m = (location.href || '').match(/reader\/([a-zA-Z0-9]+)/);
      const bid = m ? m[1] : '';
      const raw = localStorage.getItem('book:lastChapters');
      if (raw) {
        const parsed = JSON.parse(raw);
        if (bid && parsed && parsed[bid] != null) {
          obs.localIdx = parseInt(parsed[bid], 10);
          obs.bookId = obs.bookId || bid;
        }
      }
    } catch (e) {}
  }

  if (!window.__WR_OBSERVER_INSTALLED__) {
    window.__WR_OBSERVER_INSTALLED__ = true;
    installed = true;
  }
  installNavLogger();
  updateReaderId();
  updateLocalIdx();

  function toNum(v) {
    if (v === null || v === undefined || v === '') return null;
    const n = Number(v);
    return Number.isFinite(n) ? n : null;
  }

  function normalizeChapterList(list) {
    if (!Array.isArray(list) || list.length === 0) return null;
    const uidByIdx = [];
    const idxByUid = {};
    let maxIdx = -1;
    for (const item of list) {
      if (!item) continue;
      const uid = item.chapterUid ?? item.chapter_uid ?? item.uid ??
                  item.chapterId ?? item.chapter_id;
      const idx = item.chapterIdx ?? item.chapter_idx ?? item.idx ??
                  item.chapterIndex ?? item.chapter_index;
      const uidNum = toNum(uid);
      const idxNum = toNum(idx);
      if (uidNum === null || idxNum === null) continue;
      uidByIdx[idxNum] = uidNum;
      idxByUid[String(uidNum)] = idxNum;
      if (idxNum > maxIdx) maxIdx = idxNum;
    }
    if (!uidByIdx.length) return null;
    const total = Math.max(uidByIdx.length, maxIdx + 1, list.length);
    return {uidByIdx, idxByUid, total};
  }

  function markCurrentInList() {
    try {
      const list = obs.chapterInfosList;
      if (!Array.isArray(list) || list.length === 0) return;
      const curUidNum = toNum(obs.chapterUid ?? obs.chapter_uid ?? null);
      let curIdxNum = toNum(obs.chapterIdx ?? obs.idx ?? null);
      if (curIdxNum === null) curIdxNum = toNum(obs.localIdx ?? null);
      let found = false;
      list.forEach((item) => {
        if (!item) return;
        const uidNum = toNum(item.chapterUid ?? item.uid ?? null);
        const idxNum = toNum(item.chapterIdx ?? item.idx ?? null);
        let isCurrent = false;
        if (uidNum !== null && curUidNum !== null) {
          isCurrent = uidNum === curUidNum;
        } else if (idxNum !== null && curIdxNum !== null) {
          isCurrent = idxNum === curIdxNum;
        }
        item.isCurrent = isCurrent;
        if (isCurrent) {
          found = true;
          obs.currentIndex = item.index;
        }
      });
      if (!found && curIdxNum !== null && curIdxNum >= 1) {
        const idx0 = curIdxNum - 1;
        if (list[idx0]) {
          list[idx0].isCurrent = true;
          obs.currentIndex = list[idx0].index;
        }
      }
    } catch (e) {}
  }

  function updateFromProgress(payload, url) {
    try {
      const prevUid = obs.chapterUid;
      const prevIdx = obs.chapterIdx;
      let bookId = '';
      if (url && typeof url === 'string') {
        const m = url.match(/[?&]bookId=([a-zA-Z0-9]+)/);
        if (m) bookId = m[1];
      }
      const data = payload && payload.data ? payload.data : payload;
      const book = (data && data.book) ? data.book : data;
      const uid = book ? (book.chapterUid ?? book.chapter_uid) : null;
      const idx = book ? (book.chapterIdx ?? book.chapter_idx) : null;
      if (!bookId && data) {
        bookId = data.bookId || data.book_id ||
                 (data.book ? (data.book.bookId || data.book.book_id) : '');
      }
      if (uid != null) obs.chapterUid = uid;
      if (idx != null) {
        obs.chapterIdx = idx;
        obs.idx = idx;
      }
      updateReaderId();
      if (bookId) obs.bookId = obs.bookId || bookId;
      markCurrentInList();
      if (prevUid !== obs.chapterUid || prevIdx !== obs.chapterIdx) {
        try {
          console.log('[OBS] progress', 'uid', obs.chapterUid,
                      'idx', obs.chapterIdx, 'href', location.href);
        } catch (e) {}
      }
    } catch (e) {}
  }

  function updateFromChapterInfos(payload, url) {
    try {
      const data = payload && payload.data ? payload.data : payload;
      let list = null;
      let bookId = '';
      if (Array.isArray(data)) {
        for (const item of data) {
          if (!item) continue;
          if (!bookId) {
            bookId = item.bookId || item.book_id || '';
          }
          const cand = item.updated || item.chapters || item.chapterInfos ||
                       item.chapterinfos || item.chapterList || item.list;
          if (Array.isArray(cand) && cand.length) {
            list = cand;
            break;
          }
        }
      }
      if (!list && data && typeof data === 'object') {
        list = data.chapterInfos || data.chapterinfos || data.chapters ||
               data.chapterList || data.list || data.updated;
        if (!bookId) {
          bookId = data.bookId || data.book_id ||
                   (data.book ? (data.book.bookId || data.book.book_id) : '');
        }
      }
      if (!Array.isArray(list) || list.length === 0) return;
      const mapped = normalizeChapterList(list);
      if (!mapped) return;
      obs.uidByIdx = mapped.uidByIdx;
      obs.idxByUid = mapped.idxByUid;
      obs.total = mapped.total;
      obs.chapterInfosCount = list.length;
      obs.chapterInfosAt = Date.now();
      try {
        const curUidRaw = obs.chapterUid ?? obs.chapter_uid ?? null;
        const curUidNum = toNum(curUidRaw);
        let curIdxRaw = obs.chapterIdx;
        if (curIdxRaw === null || curIdxRaw === undefined) curIdxRaw = obs.idx;
        if (curIdxRaw === null || curIdxRaw === undefined) curIdxRaw = obs.localIdx;
        const curIdxNum = toNum(curIdxRaw);
        const slim = list.map((item, i) => {
          if (!item) return null;
          const uid = item.chapterUid ?? item.chapter_uid ?? item.uid ??
                      item.chapterId ?? item.chapter_id;
          const idx = item.chapterIdx ?? item.chapter_idx ?? item.idx ??
                      item.chapterIndex ?? item.chapter_index;
          const uidNum = toNum(uid);
          const idxNum = toNum(idx);
          const title = item.title || item.chapterTitle || item.name || '';
          const levelNum = toNum(item.level);
          const level = (levelNum !== null && levelNum > 0) ? levelNum : 1;
          let isCurrent = false;
          if (uidNum !== null && curUidNum !== null) {
            isCurrent = uidNum === curUidNum;
          } else if (idxNum !== null && curIdxNum !== null) {
            isCurrent = idxNum === curIdxNum;
          }
          const index = (idxNum !== null && idxNum >= 1) ? (idxNum - 1) : i;
          return {
            index,
            title,
            level,
            isCurrent,
            chapterUid: uidNum !== null ? String(uidNum)
                      : (uid != null ? String(uid) : ''),
            chapterIdx: idxNum
          };
        }).filter(Boolean);
        if (slim.length) obs.chapterInfosList = slim;
      } catch (e) {}
      updateReaderId();
      if (!bookId) {
        const m = (location.href || '').match(/reader\/([a-zA-Z0-9]+)/);
        if (m) bookId = m[1];
      }
      if (bookId) obs.bookId = obs.bookId || bookId;
      const curUidKey = obs.chapterUid != null ? String(obs.chapterUid) : '';
      if (curUidKey && obs.chapterIdx == null &&
          mapped.idxByUid[curUidKey] != null) {
        obs.chapterIdx = mapped.idxByUid[curUidKey];
        obs.idx = obs.chapterIdx;
      }
      markCurrentInList();
      try {
        console.log('[OBS] chapterInfos mapped', list.length);
        if (!obs.chapterInfosLogged) {
          obs.chapterInfosLogged = true;
          const dataKeys = data && typeof data === 'object'
            ? Object.keys(data) : [];
          const sample = list.slice(0, 2);
          console.log('[OBS_CHAPTERINFOS] keys', dataKeys.join(','));
          console.log('[OBS_CHAPTERINFOS] sample', JSON.stringify(sample));
        }
      } catch (e) {}
    } catch (e) {}
  }

  function extractUrl(resp, args) {
    try {
      if (resp && resp.url) return String(resp.url);
    } catch (e) {}
    try {
      const a0 = args && args[0];
      if (a0 && a0.url) return String(a0.url);
      if (typeof a0 === 'string') return a0;
    } catch (e) {}
    return '';
  }

  function safeDefine(obj, prop, desc) {
    try {
      Object.defineProperty(obj, prop, desc);
      return true;
    } catch (e) {
      return false;
    }
  }

  function wrapFetch(fn) {
    try {
      if (typeof fn !== 'function') return fn;
      if (fn.__wr_hooked) return fn;
      const wrapped = function(...args) {
        return fn.apply(this, args).then(resp => {
          try {
            const urlStr = extractUrl(resp, args);
            const lowerUrl = urlStr.toLowerCase();
            if (lowerUrl.includes('getprogress') || lowerUrl.includes('chapterinfos')) {
              const clone = resp && resp.clone ? resp.clone() : null;
              if (clone && clone.json) {
                clone.json().then(data => {
                  if (lowerUrl.includes('getprogress')) updateFromProgress(data, urlStr);
                  if (lowerUrl.includes('chapterinfos')) updateFromChapterInfos(data, urlStr);
                }).catch(() => {});
              }
            }
          } catch (e) {}
          return resp;
        });
      };
      wrapped.__wr_hooked = true;
      wrapped.__wr_orig = fn;
      return wrapped;
    } catch (e) {
      return fn;
    }
  }

  function ensureFetch(win) {
    try {
      if (!win) return false;
      let changed = false;
      const current = win.fetch;
      const wrapped = wrapFetch(current) || current;
      if (!win.__wr_fetch_ref) {
        win.__wr_fetch_ref = wrapped;
      } else if (wrapped && win.__wr_fetch_ref !== wrapped) {
        win.__wr_fetch_ref = wrapped;
      }
      if (win.fetch !== win.__wr_fetch_ref) {
        try { win.fetch = win.__wr_fetch_ref; changed = true; } catch (e) {}
      }
      if (!win.__wr_fetch_setter_installed__) {
        const desc = Object.getOwnPropertyDescriptor(win, 'fetch');
        const configurable = desc ? desc.configurable : true;
        const enumerable = desc ? desc.enumerable : true;
        if (configurable) {
          const ok = safeDefine(win, 'fetch', {
            configurable: true,
            enumerable,
            get() { return win.__wr_fetch_ref; },
            set(v) { win.__wr_fetch_ref = wrapFetch(v) || v; }
          });
          if (ok) {
            win.__wr_fetch_setter_installed__ = true;
            changed = true;
          }
        }
      }
      return changed;
    } catch (e) {
      return false;
    }
  }

  function wrapXhrCtor(Ctor) {
    try {
      if (typeof Ctor !== 'function' || !Ctor.prototype) return Ctor;
      const proto = Ctor.prototype;
      if (proto.open && !proto.open.__wr_hooked) {
        const origOpen = proto.open;
        const openWrapper = function(method, url) {
          this.__wr_url = url;
          return origOpen.apply(this, arguments);
        };
        openWrapper.__wr_hooked = true;
        proto.open = openWrapper;
      }
      if (proto.send && !proto.send.__wr_hooked) {
        const origSend = proto.send;
        const sendWrapper = function() {
          this.addEventListener('load', function() {
            try {
              const urlStr = this.__wr_url ? String(this.__wr_url) : '';
              const lowerUrl = urlStr.toLowerCase();
              if (lowerUrl.includes('getprogress') || lowerUrl.includes('chapterinfos')) {
                let data = null;
                if (this.responseType === 'json' && this.response) {
                  data = this.response;
                } else if (typeof this.responseText === 'string' && this.responseText) {
                  data = JSON.parse(this.responseText);
                }
                if (data) {
                  if (lowerUrl.includes('getprogress')) updateFromProgress(data, urlStr);
                  if (lowerUrl.includes('chapterinfos')) updateFromChapterInfos(data, urlStr);
                }
              }
            } catch (e) {}
          });
          return origSend.apply(this, arguments);
        };
        sendWrapper.__wr_hooked = true;
        proto.send = sendWrapper;
      }
      return Ctor;
    } catch (e) {
      return Ctor;
    }
  }

  function ensureXhr(win) {
    try {
      if (!win || typeof win.XMLHttpRequest !== 'function') return false;
      let changed = false;
      const wrapped = wrapXhrCtor(win.XMLHttpRequest) || win.XMLHttpRequest;
      if (!win.__wr_xhr_ref) {
        win.__wr_xhr_ref = wrapped;
      } else if (wrapped && win.__wr_xhr_ref !== wrapped) {
        win.__wr_xhr_ref = wrapped;
      }
      if (win.XMLHttpRequest !== win.__wr_xhr_ref) {
        try { win.XMLHttpRequest = win.__wr_xhr_ref; changed = true; } catch (e) {}
      }
      if (!win.__wr_xhr_setter_installed__) {
        const desc = Object.getOwnPropertyDescriptor(win, 'XMLHttpRequest');
        const configurable = desc ? desc.configurable : true;
        const enumerable = desc ? desc.enumerable : true;
        if (configurable) {
          const ok = safeDefine(win, 'XMLHttpRequest', {
            configurable: true,
            enumerable,
            get() { return win.__wr_xhr_ref; },
            set(v) { win.__wr_xhr_ref = wrapXhrCtor(v) || v; }
          });
          if (ok) {
            win.__wr_xhr_setter_installed__ = true;
            changed = true;
          }
        }
      }
      return changed;
    } catch (e) {
      return false;
    }
  }

  function hookWindow(win) {
    let changed = false;
    if (ensureFetch(win)) changed = true;
    if (ensureXhr(win)) changed = true;
    return changed;
  }

  let hooked = false;
  if (hookWindow(window)) hooked = true;
  try {
    const frames = Array.from(document.querySelectorAll('iframe'));
    frames.forEach(f => {
      try {
        const w = f.contentWindow;
        if (w && hookWindow(w)) hooked = true;
      } catch (e) {}
    });
  } catch (e) {}
  if (hooked) {
    window.__WR_PROGRESS_HOOK_INSTALLED__ = true;
    installed = true;
  }

  const summary = {
    bookId: obs.bookId,
    chapterUid: obs.chapterUid,
    chapterIdx: obs.chapterIdx,
    idx: obs.idx,
    total: obs.total,
    chapterInfosCount: obs.chapterInfosCount
  };
  return {ok: true, installed, obs: summary};
})()
)WR");
  return js;
}

static const char kWeReadDefaultSettingsScript[] = R"WR(
(function() {
  if (window.__WR_DEFAULT_SETTINGS_INSTALLED__) return;
  window.__WR_DEFAULT_SETTINGS_INSTALLED__ = true;
  const host = location.host || '';
  if (!/weread\.qq\.com$/i.test(host)) return;
  const path = location.pathname || '';
  const isReader = /\/web\/reader\//i.test(path);
  const targetTheme = 'white';
  let themeUpdated = false;
  let fontUpdated = false;

  function readCookie(name) {
    try {
      const raw = document.cookie || '';
      const parts = raw.split(';');
      for (let i = 0; i < parts.length; i++) {
        const p = parts[i].trim();
        if (p.startsWith(name + '=')) return p.substring(name.length + 1);
      }
    } catch (e) {}
    return '';
  }

  function setThemeCookie(value) {
    try {
      document.cookie = 'wr_theme=' + value +
        '; domain=.weread.qq.com; path=/; max-age=31536000';
    } catch (e) {}
  }

  try {
    const cookieTheme = (readCookie('wr_theme') || '').toLowerCase();
    if (cookieTheme !== targetTheme) {
      setThemeCookie(targetTheme);
      themeUpdated = true;
    }
  } catch (e) {}

  try {
    const lsTheme = (localStorage.getItem('wr_theme') || '').toLowerCase();
    if (lsTheme !== targetTheme) {
      localStorage.setItem('wr_theme', targetTheme);
      themeUpdated = true;
    }
  } catch (e) {}

  function applyThemeDom() {
    try {
      if (document.body) {
        document.body.classList.add('wr_whiteTheme');
        document.body.classList.remove('wr_darkTheme');
        document.body.setAttribute('data-theme', 'light');
      }
      if (document.documentElement) {
        document.documentElement.setAttribute('data-theme', 'light');
        document.documentElement.classList.remove('dark');
      }
    } catch (e) {}
  }

  if (isReader) {
    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', applyThemeDom, { once: true });
    } else {
      applyThemeDom();
    }
  }

  let setting = {};
  try {
    const raw = localStorage.getItem('wrLocalSetting') || '';
    if (raw) {
      const parsed = JSON.parse(raw);
      if (parsed && typeof parsed === 'object') setting = parsed;
    }
  } catch (e) {
    setting = {};
  }

  const targetFont = 'TsangerYunHei-W05';
  const currentFont = (setting.fontFamily || '').trim();
  const currentLower = currentFont.toLowerCase();
  const fontInvalid = !currentFont ||
                      currentLower === 'wr_default_font' ||
                      currentLower === 'default' ||
                      currentLower === 'system';
  if (fontInvalid) {
    setting.fontFamily = targetFont;
    fontUpdated = true;
  }

  const rawSize = setting.fontSizeLevel;
  let sizeNum = null;
  if (rawSize !== null && rawSize !== undefined && rawSize !== '') {
    const parsed = parseInt(rawSize, 10);
    if (!isNaN(parsed)) sizeNum = parsed;
  }
  if (sizeNum === null) {
    setting.fontSizeLevel = 4;
    fontUpdated = true;
  } else if (sizeNum < 1) {
    setting.fontSizeLevel = 1;
    fontUpdated = true;
  }

  if (fontUpdated) {
    try {
      localStorage.setItem('wrLocalSetting', JSON.stringify(setting));
    } catch (e) {}
  }

  if (themeUpdated || fontUpdated) {
    try {
      console.log('[DEFAULTS]', {
        themeUpdated,
        fontUpdated,
        fontSizeBefore: rawSize
      });
    } catch (e) {}
  }
})();
)WR";

const QString &weReadDefaultSettingsScriptSource() {
  static const QString js = QString::fromUtf8(kWeReadDefaultSettingsScript);
  return js;
}
} // namespace

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
  script.setSourceCode(chapterObserverScriptSource());
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
  script.setSourceCode(weReadDefaultSettingsScriptSource());
  m_view->page()->scripts().insert(script);
  m_defaultSettingsScriptInstalled = true;
  qInfo() << "[DEFAULTS] script installed";
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
      chapterObserverScriptSource(),
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

  // 同步更新 UA 配置：微信书籍与非书籍统一使用当前模式的 UA
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
    m_view->load(currentUrl);
  }
}

void WereadBrowser::logUrlState(const QString &tag) {
  const QString u = currentUrl.toString();
  qInfo() << "[URL]" << tag << u << "isWeReadBook" << isWeReadBook()
          << "isDedao" << isDedaoBook();
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
  m_fontLogged = true;
  const QString js = QStringLiteral(
      "(() => {"
      "  try {"
      "    const cont = document.querySelector('.renderTargetContent') || "
      "document.body;"
      "    const style = cont ? getComputedStyle(cont) : null;"
      "    let setting = {};"
      "    try { setting = JSON.parse(localStorage.getItem('wrLocalSetting') "
      "|| '{}'); } catch(e) { setting = {}; }"
      "    return {"
      "      fontFamily: style ? style.fontFamily : '',"
      "      fontSize: style ? style.fontSize : '',"
      "      fontWeight: style ? style.fontWeight : '',"
      "      settingFont: setting.fontFamily || '',"
      "      settingSize: setting.fontSizeLevel || ''"
      "    };"
      "  } catch(e) { return {error:String(e)}; }"
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
        QJsonObject obj = result.toJsonObject();

        QString urlStr = obj["url"].toString();

        int scrollY = obj["scrollY"].toInt();

        auto isErrorUrl = [](const QString &candidate) {
          if (candidate.isEmpty())

            return false;

          if (candidate.startsWith(QStringLiteral("chrome-error://")))

            return true;

          const QUrl parsed = QUrl::fromUserInput(candidate);

          return parsed.scheme() == QLatin1String("chrome-error");
        };

        if (isErrorUrl(urlStr)) {

          const QString filePath = WereadBrowser::getSessionStatePath();

          QString lastGoodUrl;

          QFile existing(filePath);

          if (existing.exists() &&

              existing.open(QIODevice::ReadOnly | QIODevice::Text)) {

            const QJsonDocument existingDoc =

                QJsonDocument::fromJson(existing.readAll());

            existing.close();

            const QJsonObject existingObj = existingDoc.object();

            const QString recordedGood =

                existingObj.value("lastGoodUrl").toString();

            const QString recordedUrl = existingObj.value("url").toString();

            if (!recordedGood.isEmpty() && !isErrorUrl(recordedGood)) {

              lastGoodUrl = recordedGood;

            } else if (!recordedUrl.isEmpty() && !isErrorUrl(recordedUrl)) {

              lastGoodUrl = recordedUrl;
            }
          }

          if (!lastGoodUrl.isEmpty()) {

            qInfo() << "[SESSION] Skip save (error url), keep last good:"

                    << lastGoodUrl;

          } else {

            qWarning() << "[SESSION] Skip save (error url, no last good):"

                       << urlStr;
          }

          return;
        }

        QJsonObject sessionState;

        sessionState["url"] = urlStr;

        sessionState["lastGoodUrl"] = urlStr;

        sessionState["scrollY"] = scrollY;

        sessionState["timestamp"] = now;

        QString filePath = WereadBrowser::getSessionStatePath();

        QFile file(filePath);

        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {

          QJsonDocument doc(sessionState);

          file.write(doc.toJson());

          file.close();

          // 更新保存时间戳（mutable 允许在 const 方法中修改）

          m_lastSessionSaveTime = now;

          qInfo() << "[SESSION] Saved session state:" << urlStr

                  << "scrollY:" << scrollY;
        }
      });
}
