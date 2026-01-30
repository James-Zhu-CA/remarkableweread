#include "weread_browser.h"
#include <QString>

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

static const char kDedaoDefaultSettingsScript[] = R"WR(
(function() {
  if (window.__DEDAO_DEFAULT_SETTINGS_INSTALLED__) return;
  window.__DEDAO_DEFAULT_SETTINGS_INSTALLED__ = true;
  const host = location.host || '';
  if (!/dedao\.cn$/i.test(host)) return;
  const path = location.pathname || '';
  const isReader = /\/ebook\/reader/i.test(path);
  if (!isReader) return;

  const styleId = '__dedao_clear_bg';
  if (!document.getElementById(styleId)) {
    const style = document.createElement('style');
    style.id = styleId;
    style.textContent =
      'html,body,#app,.app_content,.content,.iget-reader-book,.reader-book-wrapper,.reader-content,.reader-body{' +
      'background:#fff !important;background-color:#fff !important;background-image:none !important;' +
      '}';
    const root = document.head || document.documentElement;
    if (root) root.appendChild(style);
  }
  const boldStyleId = '__dedao_text_bold';
  if (!document.getElementById(boldStyleId)) {
    const style = document.createElement('style');
    style.id = boldStyleId;
    style.textContent =
      '.iget-reader-book *:not(.iconfont),' +
      '.reader-book-wrapper *:not(.iconfont),' +
      '.readerChapterContent *:not(.iconfont),' +
      '.reader-content *:not(.iconfont),' +
      '.reader-body *:not(.iconfont){' +
      'text-shadow:0 0 0.6px rgba(0,0,0,0.85) !important;' +
      '}';
    const root = document.head || document.documentElement;
    if (root) root.appendChild(style);
  }
  try {
    console.log('[DEDAO_STYLE_BOOT]', {
      clearStyle: !!document.getElementById(styleId),
      boldStyle: !!document.getElementById(boldStyleId)
    });
  } catch (e) {}

  let setting = {};
  let updated = false;
  try {
    const raw = localStorage.getItem('readerSettings') || '';
    if (raw) {
      const parsed = JSON.parse(raw);
      if (parsed && typeof parsed === 'object') setting = parsed;
    }
  } catch (e) {
    setting = {};
  }

  const targetFont = 'XiTongZiTi';
  const currentFont = (setting.fontFamily || '').trim();
  if (currentFont !== targetFont) {
    setting.fontFamily = targetFont;
    updated = true;
  }

  const rawLevel = setting.fontLevel;
  let levelNum = null;
  if (rawLevel !== null && rawLevel !== undefined && rawLevel !== '') {
    const parsed = parseInt(rawLevel, 10);
    if (!isNaN(parsed)) levelNum = parsed;
  }
  if (levelNum === null) {
    setting.fontLevel = 7;
    updated = true;
  } else if (levelNum < 1) {
    setting.fontLevel = 7;
    updated = true;
  }

  if (updated) {
    try {
      localStorage.setItem('readerSettings', JSON.stringify(setting));
      console.log('[DEDAO_DEFAULTS]', {
        fontUpdated: updated,
        fontLevelBefore: rawLevel
      });
    } catch (e) {}
  }
})();
)WR";

const QString &dedaoDefaultSettingsScriptSource() {
  static const QString js = QString::fromUtf8(kDedaoDefaultSettingsScript);
  return js;
}
} // namespace

// Provide access to script sources for WereadBrowser
const QString &WereadBrowser::getChapterObserverScript() {
  return chapterObserverScriptSource();
}

const QString &WereadBrowser::getDefaultSettingsScript() {
  return weReadDefaultSettingsScriptSource();
}

const QString &WereadBrowser::getDedaoDefaultSettingsScript() {
  return dedaoDefaultSettingsScriptSource();
}
