#include "weread_browser.h"
#include <QTextStream>

namespace {
struct SessionStateData {
  QString url;
  QString lastGoodUrl;
  QString lastBookUrl;
  int scrollY = 0;
  int lastBookScrollY = 0;
  bool hasLastBookScroll = false;
  qint64 timestamp = 0;
};

bool isErrorUrlString(const QString &candidate) {
  if (candidate.isEmpty())
    return false;
  if (candidate.startsWith(QStringLiteral("chrome-error://")))
    return true;
  const QUrl parsed = QUrl::fromUserInput(candidate);
  return parsed.scheme() == QLatin1String("chrome-error");
}

bool isBookUrlString(const QString &candidate) {
  if (candidate.isEmpty())
    return false;
  const bool isWeRead = candidate.contains(QStringLiteral("weread.qq.com")) &&
                        candidate.contains(QStringLiteral("/web/reader"));
  const bool isDedao = candidate.contains(QStringLiteral("dedao.cn")) &&
                       candidate.contains(QStringLiteral("/ebook/reader"));
  return isWeRead || isDedao;
}

SessionStateData readSessionStateFile(const QString &filePath) {
  SessionStateData state;
  QFile file(filePath);
  if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
    return state;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  file.close();
  const QJsonObject obj = doc.object();
  state.url = obj.value("url").toString();
  state.lastGoodUrl = obj.value("lastGoodUrl").toString();
  state.lastBookUrl = obj.value("lastBookUrl").toString();
  state.scrollY = obj.value("scrollY").toInt();
  if (obj.contains("lastBookScrollY")) {
    state.lastBookScrollY = obj.value("lastBookScrollY").toInt();
    state.hasLastBookScroll = true;
  }
  state.timestamp = obj.value("timestamp").toVariant().toLongLong();
  return state;
}

QString selectRestoreUrl(const SessionStateData &state, bool *usedLastGood,
                         bool *usedLastBook) {
  if (usedLastGood)
    *usedLastGood = false;
  if (usedLastBook)
    *usedLastBook = false;

  QString candidate = state.url;
  if (candidate.isEmpty() || isErrorUrlString(candidate)) {
    if (!state.lastGoodUrl.isEmpty() &&
        !isErrorUrlString(state.lastGoodUrl)) {
      candidate = state.lastGoodUrl;
      if (usedLastGood)
        *usedLastGood = true;
    } else {
      candidate.clear();
    }
  }

  const bool hasLastBook =
      !state.lastBookUrl.isEmpty() && isBookUrlString(state.lastBookUrl) &&
      !isErrorUrlString(state.lastBookUrl);
  if (hasLastBook && (candidate.isEmpty() || !isBookUrlString(candidate))) {
    candidate = state.lastBookUrl;
    if (usedLastBook)
      *usedLastBook = true;
  }

  return candidate;
}
} // namespace

QUrl WereadBrowser::loadSessionUrl() const {
  const SessionStateData state =
      readSessionStateFile(WereadBrowser::getSessionStatePath());
  bool usedLastGood = false;
  bool usedLastBook = false;
  const QString candidateUrl =
      selectRestoreUrl(state, &usedLastGood, &usedLastBook);

  if (usedLastGood)
    qInfo() << "[SESSION] Fallback to last good URL:" << state.lastGoodUrl;
  if (usedLastBook)
    qInfo() << "[SESSION] Prefer last book URL:" << state.lastBookUrl;

  if (candidateUrl.isEmpty()) {
    if (!state.url.isEmpty() && isErrorUrlString(state.url)) {
      qWarning() << "[SESSION] Skip restoring error URL:" << state.url;
    }
    return QUrl();
  }

  const QUrl url = QUrl::fromUserInput(candidateUrl);
  if (!url.isValid())
    return QUrl();

  const qint64 timestamp = state.timestamp;
  const qint64 age = QDateTime::currentMSecsSinceEpoch() - timestamp;

  // 只恢复 24 小时内的会话
  if (timestamp > 0 && age < 24 * 3600 * 1000) {
    qInfo() << "[SESSION] Restoring session URL:" << url.toString() << "age:"
            << (age / 1000) << "s";
    return url;
  }

  qInfo() << "[SESSION] Session too old, ignoring (age:" << (age / 1000)
          << "s)";
  return QUrl();
}





int WereadBrowser::loadSessionScrollPosition() const {
  const SessionStateData state =
      readSessionStateFile(WereadBrowser::getSessionStatePath());
  bool usedLastGood = false;
  bool usedLastBook = false;
  const QString candidateUrl =
      selectRestoreUrl(state, &usedLastGood, &usedLastBook);
  if (candidateUrl.isEmpty())
    return 0;
  if (usedLastBook)
    return state.hasLastBookScroll ? state.lastBookScrollY : 0;
  return state.scrollY;
}
