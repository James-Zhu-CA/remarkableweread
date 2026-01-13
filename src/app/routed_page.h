#ifndef ROUTED_PAGE_H
#define ROUTED_PAGE_H

#include <QDateTime>
#include <QDebug>
#include <QUrl>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

// 页面路由控制：处理窗口创建和导航请求
class RoutedPage : public QWebEnginePage {
  Q_OBJECT
public:
  RoutedPage(QWebEngineProfile *profile, QWebEngineView *view,
             QObject *parent = nullptr)
      : QWebEnginePage(profile, parent), m_view(view) {}

signals:
  void smartRefreshEvents(const QString &json);
  void smartRefreshBurstEnd();
  void jsUnexpectedEnd(const QString &message, const QString &source);
  void chapterInfosMapped(int count);

protected:
  QWebEnginePage *createWindow(WebWindowType type) override;
  bool acceptNavigationRequest(const QUrl &url, NavigationType type,
                               bool isMainFrame) override;
  void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                const QString &message, int lineNumber,
                                const QString &sourceID) override;

private:
  QWebEngineView *m_view = nullptr;
  qint64 m_bookEnterTs = 0;
  qint64 m_lastReloadTs = 0;
};

#endif // ROUTED_PAGE_H
