#include "routed_page.h"
#include "common.h"

QWebEnginePage *RoutedPage::createWindow(WebWindowType type) {
  Q_UNUSED(type);
  const QString currentUrl = m_view ? m_view->url().toString() : QString();
  const bool isDedaoBook = currentUrl.contains(QStringLiteral("dedao.cn")) &&
                           currentUrl.contains(QStringLiteral("/ebook/reader"));

  if (isDedaoBook) {
    qInfo() << "[WINDOW] blocked popup on dedao book page" << currentUrl;
    return nullptr;
  }

  auto *tmp = new QWebEnginePage(profile(), this);
  connect(tmp, &QWebEnginePage::urlChanged, this, [this, tmp](const QUrl &url) {
    if (m_view) {
      qInfo() << "[WINDOW] redirect new window to current view" << url;
      m_view->load(url);
    }
    tmp->deleteLater();
  });
  return tmp;
}

bool RoutedPage::acceptNavigationRequest(const QUrl &url, NavigationType type,
                                         bool isMainFrame) {
  if (!isMainFrame)
    return true;
  const QString currentUrl = m_view ? m_view->url().toString() : QString();
  const qint64 ts = QDateTime::currentMSecsSinceEpoch();
  const qint64 rel = m_lastReloadTs ? (ts - m_lastReloadTs) : -1;
  qInfo() << "[NAV] request" << url << "type" << type << "mainFrame"
          << isMainFrame << "ts" << ts << "sinceReload" << rel << "from"
          << currentUrl;
  const bool inDedaoReader =
      currentUrl.contains(QStringLiteral("dedao.cn")) &&
      currentUrl.contains(QStringLiteral("/ebook/reader"));
  if (inDedaoReader) {
    const QString target = url.toString();
    const bool sameReader = target.contains(QStringLiteral("dedao.cn")) &&
                            target.contains(QStringLiteral("/ebook/reader"));
    const bool isDetail = target.contains(QStringLiteral("/ebook/detail"));
    if (!sameReader) {
      const qint64 allowUntil =
          property("allow_non_reader_nav_until").toLongLong();
      const bool allowMenuToggle = allowUntil > 0 && ts <= allowUntil;
      const bool isWeReadHome =
          target == QStringLiteral("https://weread.qq.com/") ||
          target == QStringLiteral("https://weread.qq.com");
      if (allowMenuToggle && isWeReadHome) {
        setProperty("allow_non_reader_nav_until", 0);
        qInfo() << "[NAV] allow non-reader nav from dedao (menu toggle)"
                << url << "ts" << ts << "sinceReload" << rel;
        return true;
      }
      qInfo() << "[NAV] block non-reader nav from dedao" << url << "ts" << ts
              << "sinceReload" << rel;
      return false;
    }
    if (isDetail) {
      qInfo() << "[NAV] block detail nav inside reader" << url << "ts" << ts
              << "sinceReload" << rel;
      return false;
    }
  }
  return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}

void RoutedPage::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                          const QString &message,
                                          int lineNumber,
                                          const QString &sourceID) {
  const bool isWeReadSrc =
      sourceID.contains(QStringLiteral("weread.qq.com"), Qt::CaseInsensitive);
  if (isWeReadSrc && message.contains(QStringLiteral("Unexpected end of input"),
                                      Qt::CaseInsensitive)) {
    emit jsUnexpectedEnd(message, sourceID);
  }

  if (message.startsWith(QStringLiteral("[REFRESH_EVENTS]"))) {
    const QString json = message.mid(16);
    emit smartRefreshEvents(json);
  }
  if (message == QStringLiteral("[REFRESH_BURST_END]")) {
    emit smartRefreshBurstEnd();
  }
  if (message.startsWith(QStringLiteral("[OBS] chapterInfos mapped"))) {
    const QString countStr = message.section(' ', -1);
    bool ok = false;
    const int count = countStr.toInt(&ok);
    emit chapterInfosMapped(ok ? count : -1);
  }
  const bool logInfo = logLevelAtLeast(LogLevel::Info);
  const bool logWarn = logLevelAtLeast(LogLevel::Warning);
  const bool isNoisy = message.startsWith(QStringLiteral("[REFRESH_EVENTS]")) ||
                       message.startsWith(QStringLiteral("[DOM_SCORE]"));
  const bool isTagged = message.startsWith(QStringLiteral("["));
  if (level == QWebEnginePage::ErrorMessageLevel ||
      level == QWebEnginePage::WarningMessageLevel) {
    if (logWarn) {
      qWarning().noquote() << "[JS]" << sourceID << ":" << lineNumber
                           << message;
    }
  } else {
    if (logInfo && isTagged && !isNoisy) {
      qInfo().noquote() << "[JS]" << sourceID << ":" << lineNumber << message;
    }
  }
}
