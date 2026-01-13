#ifndef RESOURCE_INTERCEPTOR_H
#define RESOURCE_INTERCEPTOR_H

#include <QDateTime>
#include <QDebug>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>

// 条件编译开关：取消注释下行以启用详细资源日志（会影响性能）
// #define WEREAD_DEBUG_RESOURCES

// 网络请求拦截器：阻止不必要的大字体和资源
class ResourceInterceptor : public QWebEngineUrlRequestInterceptor {
  Q_OBJECT
public:
  explicit ResourceInterceptor(QObject *parent = nullptr)
      : QWebEngineUrlRequestInterceptor(parent) {}

  void interceptRequest(QWebEngineUrlRequestInfo &info) override;
};

#endif // RESOURCE_INTERCEPTOR_H
