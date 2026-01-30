#include "resource_interceptor.h"
#include "weread_browser.h"

void ResourceInterceptor::interceptRequest(QWebEngineUrlRequestInfo &info) {
  const QString url = info.requestUrl().toString();

#ifdef WEREAD_DEBUG_RESOURCES
  const qint64 reqStartTs = QDateTime::currentMSecsSinceEpoch();
  const QString resourceTypeStr = [&]() {
    switch (info.resourceType()) {
    case QWebEngineUrlRequestInfo::ResourceTypeMainFrame:
      return "MainFrame";
    case QWebEngineUrlRequestInfo::ResourceTypeNavigationPreloadMainFrame:
      return "NavPreloadMainFrame";
    case QWebEngineUrlRequestInfo::ResourceTypeNavigationPreloadSubFrame:
      return "NavPreloadSubFrame";
    case QWebEngineUrlRequestInfo::ResourceTypeSubFrame:
      return "SubFrame";
    case QWebEngineUrlRequestInfo::ResourceTypeStylesheet:
      return "Stylesheet";
    case QWebEngineUrlRequestInfo::ResourceTypeScript:
      return "Script";
    case QWebEngineUrlRequestInfo::ResourceTypeImage:
      return "Image";
    case QWebEngineUrlRequestInfo::ResourceTypeFontResource:
      return "Font";
    case QWebEngineUrlRequestInfo::ResourceTypeMedia:
      return "Media";
    case QWebEngineUrlRequestInfo::ResourceTypeXhr:
      return "XHR";
    default:
      return "Other";
    }
  }();
  qInfo() << "[RESOURCE_START]" << resourceTypeStr << url.mid(0, 100) << "ts"
          << reqStartTs;
#endif

  auto resourceTypeStr = [&]() {
    switch (info.resourceType()) {
    case QWebEngineUrlRequestInfo::ResourceTypeMainFrame:
      return "MainFrame";
    case QWebEngineUrlRequestInfo::ResourceTypeNavigationPreloadMainFrame:
      return "NavPreloadMainFrame";
    case QWebEngineUrlRequestInfo::ResourceTypeNavigationPreloadSubFrame:
      return "NavPreloadSubFrame";
    case QWebEngineUrlRequestInfo::ResourceTypeSubFrame:
      return "SubFrame";
    case QWebEngineUrlRequestInfo::ResourceTypeStylesheet:
      return "Stylesheet";
    case QWebEngineUrlRequestInfo::ResourceTypeScript:
      return "Script";
    case QWebEngineUrlRequestInfo::ResourceTypeImage:
      return "Image";
    case QWebEngineUrlRequestInfo::ResourceTypeFontResource:
      return "Font";
    case QWebEngineUrlRequestInfo::ResourceTypeMedia:
      return "Media";
    case QWebEngineUrlRequestInfo::ResourceTypeXhr:
      return "XHR";
    default:
      return "Other";
    }
  };

  if (info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeMainFrame ||
      info.resourceType() ==
          QWebEngineUrlRequestInfo::ResourceTypeNavigationPreloadMainFrame) {
    const QUrl firstParty = info.firstPartyUrl();
    const QUrl initiator = info.initiator();
    const QUrl current = m_browser ? m_browser->currentUrlForLog() : QUrl();
    const QString reason = m_browser ? m_browser->navReason() : QString();
    const QUrl reasonTarget =
        m_browser ? m_browser->navReasonTarget() : QUrl();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 reasonAge =
        m_browser ? (now - m_browser->navReasonTs()) : -1;
    qInfo() << "[RESOURCE_MAIN]" << "url" << url
            << "navType" << info.navigationType()
            << "firstParty" << firstParty << "initiator" << initiator
            << "method" << info.requestMethod()
            << "current" << current << "reason" << reason
            << "reasonTarget" << reasonTarget << "reasonAgeMs" << reasonAge;
  }

  // Block winktemplaterendersvr fonts/media (KaTeX related)
  if (url.contains(QStringLiteral("/winktemplaterendersvr/")) &&
      (info.resourceType() ==
           QWebEngineUrlRequestInfo::ResourceTypeFontResource ||
       info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeMedia)) {
    qWarning() << "[BLOCK]" << resourceTypeStr() << url;
    info.block(true);
    return;
  }

  // Block SourceHanSerif heavy fonts (~10MB each)
  if (url.contains(QStringLiteral("SourceHanSerif"))) {
    qWarning() << "[BLOCK]" << resourceTypeStr() << url;
    info.block(true);
    return;
  }

  if (url.contains(QStringLiteral("TsangerYunHei-W05")) &&
      info.resourceType() ==
          QWebEngineUrlRequestInfo::ResourceTypeFontResource) {
    qInfo() << "[FONT_ALLOW]" << resourceTypeStr() << url;
  }

  if (url.contains(QStringLiteral("chapterInfos"), Qt::CaseInsensitive)) {
    qInfo() << "[CHAPTER_NET]" << resourceTypeStr() << url;
  }

  // 进度接口早期记录（包括 ServiceWorker 发出的请求）
  if (url.contains(QStringLiteral("/web/book/read")) ||
      url.contains(QStringLiteral("getProgress"))) {
    qInfo() << "[PROGRESS_NET]" << resourceTypeStr() << url;
  }

#ifdef WEREAD_DEBUG_RESOURCES
  qInfo() << "[RESOURCE_ALLOWED]" << resourceTypeStr << "ts" << reqStartTs;
#endif
}
