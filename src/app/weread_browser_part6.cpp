#include "weread_browser.h"
#include <QTextStream>

QUrl WereadBrowser::loadSessionUrl() const {

  QString filePath = WereadBrowser::getSessionStatePath();

  QFile file(filePath);

  if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());

    file.close();

    QJsonObject obj = doc.object();

    auto isErrorUrl = [](const QString &candidate) {

      if (candidate.isEmpty())

        return false;

      if (candidate.startsWith(QStringLiteral("chrome-error://")))

        return true;

      const QUrl parsed = QUrl::fromUserInput(candidate);

      return parsed.scheme() == QLatin1String("chrome-error");

    };

    QString urlStr = obj["url"].toString();

    const QString lastGoodUrl = obj["lastGoodUrl"].toString();

    QString candidateUrl = urlStr;

    if (candidateUrl.isEmpty() || isErrorUrl(candidateUrl)) {

      if (!lastGoodUrl.isEmpty() && !isErrorUrl(lastGoodUrl)) {

        candidateUrl = lastGoodUrl;

        qInfo() << "[SESSION] Fallback to last good URL:" << candidateUrl;

      } else {

        if (!candidateUrl.isEmpty()) {

          qWarning() << "[SESSION] Skip restoring error URL:" << candidateUrl;

        }

        candidateUrl.clear();

      }

    }

    if (!candidateUrl.isEmpty()) {

      QUrl url = QUrl::fromUserInput(candidateUrl);

      if (url.isValid()) {

        qint64 timestamp = obj["timestamp"].toVariant().toLongLong();

        qint64 age = QDateTime::currentMSecsSinceEpoch() - timestamp;

        // 只恢复 24 小时内的会话

        if (age < 24 * 3600 * 1000) {

          qInfo() << "[SESSION] Restoring session URL:" << url.toString()

                  << "age:" << (age / 1000) << "s";

          return url;

        } else {

          qInfo() << "[SESSION] Session too old, ignoring (age:" << (age / 1000)

                  << "s)";

        }

      }

    }

  }

  return QUrl();

}





int WereadBrowser::loadSessionScrollPosition() const {


  QString filePath = WereadBrowser::getSessionStatePath();

  QFile file(filePath);

  if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());

    file.close();

    QJsonObject obj = doc.object();

    return obj["scrollY"].toInt();

  }

  return 0;

}
