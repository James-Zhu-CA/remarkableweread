#include "smart_refresh.h"
#include <QDateTime>
#include <QDebug>
#include <climits>

namespace {
const char *refreshEventTypeName(SmartRefreshManager::RefreshEvent::Type type) {
  switch (type) {
  case SmartRefreshManager::RefreshEvent::DOM_CHANGE:
    return "DOM_CHANGE";
  case SmartRefreshManager::RefreshEvent::SCROLL:
    return "SCROLL";
  case SmartRefreshManager::RefreshEvent::PAGE_TURN:
    return "PAGE_TURN";
  case SmartRefreshManager::RefreshEvent::MENU:
    return "MENU";
  case SmartRefreshManager::RefreshEvent::LOAD_FINISHED:
    return "LOAD_FINISHED";
  case SmartRefreshManager::RefreshEvent::BURST_END:
    return "BURST_END";
  case SmartRefreshManager::RefreshEvent::IDLE:
    return "IDLE";
  case SmartRefreshManager::RefreshEvent::CONTENT_READY:
    return "CONTENT_READY";
  }
  return "UNKNOWN";
}

QString regionToString(const QRect &region) {
  if (region.isNull()) {
    return QStringLiteral("none");
  }
  return QStringLiteral("%1,%2 %3x%4")
      .arg(region.x())
      .arg(region.y())
      .arg(region.width())
      .arg(region.height());
}
} // namespace

SmartRefreshManager::SmartRefreshManager(FbRefreshHelper *fb, int w, int h,
                                         const QString &tag, QObject *parent)
    : QObject(parent), m_fb(fb), m_width(w), m_height(h), m_tag(tag) {
  // 批处理定时器：200ms 窗口
  m_batchTimer.setSingleShot(true);
  m_batchTimer.setInterval(200);
  connect(&m_batchTimer, &QTimer::timeout, this,
          &SmartRefreshManager::processBatch);

  // 空闲检测定时器：60秒无操作
  m_idleTimer.setSingleShot(true);
  m_idleTimer.setInterval(60000);
  connect(&m_idleTimer, &QTimer::timeout, this, &SmartRefreshManager::onIdle);

  // 点击翻页后的延迟 A2 刷新
  m_postClickA2Timer.setSingleShot(true);
  m_postClickA2Timer.setInterval(kPostClickA2DelayMs);
  connect(&m_postClickA2Timer, &QTimer::timeout, this,
          &SmartRefreshManager::performPostClickA2);

  m_dedaoFallback1s.setSingleShot(true);
  m_dedaoFallback2s.setSingleShot(true);
  m_dedaoFallback3s.setSingleShot(true);
  connect(&m_dedaoFallback1s, &QTimer::timeout, this, [this]() {
    triggerDedaoDuRefresh(QStringLiteral("fallback_1s"));
    updateDedaoFallbackPending();
  });
  connect(&m_dedaoFallback2s, &QTimer::timeout, this, [this]() {
    triggerDedaoDuRefresh(QStringLiteral("fallback_2s"));
    updateDedaoFallbackPending();
  });
  connect(&m_dedaoFallback3s, &QTimer::timeout, this, [this]() {
    triggerDedaoDuRefresh(QStringLiteral("fallback_3s"));
    updateDedaoFallbackPending();
  });

  m_dedaoDelayedRefresh.setSingleShot(true);
  connect(&m_dedaoDelayedRefresh, &QTimer::timeout, this, [this]() {
    const QString reason =
        m_dedaoDelayedRefresh.property("reason").toString();
    triggerDedaoDuRefresh(reason.isEmpty() ? QStringLiteral("scroll_delta")
                                          : reason);
  });
  m_dedaoIdleRefresh.setSingleShot(true);
  connect(&m_dedaoIdleRefresh, &QTimer::timeout, this, [this]() {
    triggerDedaoDuRefresh(QStringLiteral("scroll_idle"));
  });
  m_dedaoMutationRefresh.setSingleShot(true);
  connect(&m_dedaoMutationRefresh, &QTimer::timeout, this, [this]() {
    triggerDedaoDuRefresh(QStringLiteral("dom_mutation"));
  });

  m_dedaoScrollSeriesTimer.setSingleShot(true);
  m_dedaoScrollSeriesTimer.setInterval(kDedaoScrollSeriesIntervalMs);
  connect(&m_dedaoScrollSeriesTimer, &QTimer::timeout, this, [this]() {
    if (!(m_tag == QStringLiteral("dedao") && m_isBookPage)) {
      m_dedaoScrollSeriesRemaining = 0;
      return;
    }
    if (m_dedaoScrollSeriesRemaining <= 0) {
      return;
    }
    triggerDedaoDuRefresh(QStringLiteral("scroll_series"));
    m_dedaoScrollSeriesRemaining--;
    qInfo() << "[SMART_REFRESH]" << m_tag
            << "dedao scroll series refresh, remaining"
            << m_dedaoScrollSeriesRemaining;
    if (m_dedaoScrollSeriesRemaining > 0) {
      m_dedaoScrollSeriesTimer.start();
    }
  });

  m_lastFullRefresh.start();
  m_lastActivityTime.start();
}

void SmartRefreshManager::setPostClickA2Enabled(bool enabled) {
  if (m_postClickA2Enabled == enabled) {
    return;
  }
  m_postClickA2Enabled = enabled;
  if (!m_postClickA2Enabled) {
    m_postClickA2Pending = false;
    m_postClickA2Count = 0;
    m_postClickA2Timer.stop();
  }
}

void SmartRefreshManager::setBookPage(bool isBook) {
  m_isBookPage = isBook;
  if (!isBook) {
    m_lastRefreshScore = 0;
    m_clickPending = false;
    m_clickRefreshCount = -1;
    m_postClickA2Pending = false;
    m_postClickA2Timer.stop();
    cancelDedaoScrollSeries();
  }
}

void SmartRefreshManager::resetScoreThreshold() {
  if (!m_isBookPage) {
    return;
  }
  m_lastRefreshScore = 0;
  m_clickPending = true;
  m_clickRefreshCount = 0;
  m_postClickA2Pending = true;
  m_postClickA2Timer.stop();
  cancelDedaoScrollSeries();
}

void SmartRefreshManager::pushEvent(const RefreshEvent &event) {
  m_eventQueue.append(event);
  m_lastActivityTime.restart();
  m_idleTimer.start();
  qInfo() << "[SMART_REFRESH]" << m_tag << "pushEvent"
          << refreshEventTypeName(event.type) << "score" << event.score
          << "scroll" << event.scrollDelta << "region"
          << regionToString(event.region) << "queue" << m_eventQueue.size()
          << "book" << m_isBookPage << "clickPending" << m_clickPending
          << "clickRefreshCount" << m_clickRefreshCount << "lastScore"
          << m_lastRefreshScore;

  if (event.type == RefreshEvent::LOAD_FINISHED ||
      event.type == RefreshEvent::BURST_END ||
      event.type == RefreshEvent::CONTENT_READY) {
    processBatch();
    return;
  }

  if (!m_batchTimer.isActive()) {
    m_batchTimer.start();
  }
}

void SmartRefreshManager::parseJsEvents(const QString &json) {
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError) {
    qWarning() << "[SMART_REFRESH] JSON parse error:" << err.errorString()
               << "JSON:" << json;
    return;
  }

  if (!doc.isArray()) {
    qWarning() << "[SMART_REFRESH] JSON is not an array:" << json;
    return;
  }

  QJsonArray arr = doc.array();
  for (const QJsonValue &val : arr) {
    if (!val.isObject())
      continue;
    QJsonObject obj = val.toObject();
    QString type = obj.value("t").toString();

    RefreshEvent event;
    if (type == "dom") {
      if (m_tag == QStringLiteral("dedao") && m_isBookPage) {
        continue;
      }
      event.type = RefreshEvent::DOM_CHANGE;
      event.score = obj.value("s").toInt();
      QJsonArray regions = obj.value("r").toArray();
      if (!regions.isEmpty()) {
        int minX = INT_MAX, minY = INT_MAX, maxX = 0, maxY = 0;
        for (const QJsonValue &rv : regions) {
          QJsonObject r = rv.toObject();
          int x = r.value("x").toInt();
          int y = r.value("y").toInt();
          int w = r.value("w").toInt();
          int h = r.value("h").toInt();
          minX = qMin(minX, x);
          minY = qMin(minY, y);
          maxX = qMax(maxX, x + w);
          maxY = qMax(maxY, y + h);
        }
        if (minX < INT_MAX) {
          event.region = QRect(minX, minY, maxX - minX, maxY - minY);
        }
      }
    } else if (type == "scroll") {
      if (m_tag == QStringLiteral("dedao") && m_isBookPage) {
        continue;
      }
      event.type = RefreshEvent::SCROLL;
      event.scrollDelta = static_cast<int>(obj.value("d").toDouble());
    } else if (type == "trace") {
      const QString reason = obj.value("reason").toString();
      QDebug log = qInfo();
      log << "[SMART_REFRESH]" << m_tag << "trace"
          << "host" << obj.value("host").toString()
          << "path" << obj.value("path").toString()
          << "flag" << obj.value("flag").toString()
          << "dedao" << obj.value("dedao").toBool()
          << "weread" << obj.value("weread").toBool()
          << "reason" << reason
          << "targetTag" << obj.value("targetTag").toString()
          << "targetClass" << obj.value("targetClass").toString()
          << "scrollTop" << obj.value("scrollTop").toInt()
          << "delta" << obj.value("delta").toInt()
          << "clickSeq" << obj.value("clickSeq").toInt();
      if (reason == QStringLiteral("scroll_event")) {
        log << "scrollElTag" << obj.value("scrollElTag").toString()
            << "scrollElId" << obj.value("scrollElId").toString()
            << "scrollElClass" << obj.value("scrollElClass").toString()
            << "scrollElUid" << obj.value("scrollElUid").toString()
            << "scrollElIsConnected"
            << obj.value("scrollElIsConnected").toBool()
            << "scrollElScrollHeight"
            << obj.value("scrollElScrollHeight").toInt()
            << "scrollElClientHeight"
            << obj.value("scrollElClientHeight").toInt()
            << "scrollElOffsetHeight"
            << obj.value("scrollElOffsetHeight").toInt()
            << "scrollElRectTop" << obj.value("scrollElRectTop").toInt()
            << "scrollElRectHeight"
            << obj.value("scrollElRectHeight").toInt();
      }
      if (m_tag == QStringLiteral("dedao") && m_isBookPage) {
        if (reason == QStringLiteral("scroll_event")) {
          const int delta = obj.value("delta").toInt();
          const int clickSeq = obj.value("clickSeq").toInt(-1);
          if (clickSeq >= 0 && clickSeq != m_lastDedaoClickSeq) {
            m_lastDedaoClickSeq = clickSeq;
            m_dedaoClickHandled = false;
            m_dedaoBypassThrottleOnce = true;
          }
          if (delta >= 0) {
            if (!m_dedaoClickHandled) {
              scheduleDedaoDelayedRefresh(QStringLiteral("scroll_delta"));
              startDedaoScrollSeries();
              m_dedaoClickHandled = true;
            }
          }
          continue;
        }
        if (reason == QStringLiteral("scroll_idle")) {
          m_dedaoIdleRefresh.stop();
          m_dedaoIdleRefresh.start(50);
          continue;
        }
        if (reason == QStringLiteral("dom_mutation_book") ||
            reason == QStringLiteral("dom_mutation_wrapper") ||
            reason == QStringLiteral("dom_mutation_chapter")) {
          m_dedaoMutationRefresh.stop();
          m_dedaoMutationRefresh.start(50);
          continue;
        }
        if (reason == QStringLiteral("content_ready")) {
          triggerDedaoDuRefresh(QStringLiteral("content_ready"));
          continue;
        }
        continue;
      }
      continue;
    } else {
      continue;
    }
    pushEvent(event);
  }
}

void SmartRefreshManager::triggerPageTurn() {
  if (m_tag == QStringLiteral("dedao") && m_isBookPage) {
    cancelDedaoFallbacks();
    cancelDedaoDelayedRefresh();
    cancelDedaoScrollSeries();
    return;
  }
  RefreshEvent e;
  e.type = RefreshEvent::PAGE_TURN;
  pushEvent(e);
}

void SmartRefreshManager::triggerLoadFinished() {
  RefreshEvent e;
  e.type = RefreshEvent::LOAD_FINISHED;
  pushEvent(e);
}

void SmartRefreshManager::triggerMenu() {
  RefreshEvent e;
  e.type = RefreshEvent::MENU;
  pushEvent(e);
}

void SmartRefreshManager::triggerBurstEnd() {
  RefreshEvent e;
  e.type = RefreshEvent::BURST_END;
  pushEvent(e);
}

void SmartRefreshManager::triggerContentReady() {
  RefreshEvent e;
  e.type = RefreshEvent::CONTENT_READY;
  pushEvent(e);
}

void SmartRefreshManager::triggerDedaoDuRefresh(const QString &reason) {
  if (!m_fb || !m_isBookPage) {
    return;
  }
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const bool forceBypass =
      (reason == QStringLiteral("scroll_idle") ||
       reason == QStringLiteral("dom_mutation"));
  const bool bypassOnce =
      m_dedaoBypassThrottleOnce && (reason == QStringLiteral("scroll_delta"));
  if (bypassOnce) {
    m_dedaoBypassThrottleOnce = false;
  }
  if (!forceBypass && !bypassOnce && m_lastDedaoDuRefreshMs != 0 &&
      (now - m_lastDedaoDuRefreshMs) < kDedaoDuThrottleMs) {
    qInfo() << "[SMART_REFRESH]" << m_tag << "dedao DU skip (throttle)"
            << "reason" << reason << "elapsed"
            << (now - m_lastDedaoDuRefreshMs);
    return;
  }
  if (m_ghostingRisk >= 3.0f) {
    m_fb->refreshFull(m_width, m_height);
    m_partialCount = 0;
    m_duCount = 0;
    m_ghostingRisk = 0.0f;
    m_lastFullRefresh.restart();
    m_lastRefreshTime.restart();
    m_lastDedaoDuRefreshMs = now;
    qInfo() << "[SMART_REFRESH]" << m_tag << "dedao full refresh (ghost)"
            << "reason" << reason;
    return;
  }
  m_fb->refreshUI(0, 0, m_width, m_height);
  m_duCount++;
  m_ghostingRisk += 0.01f;
  m_lastRefreshTime.restart();
  m_lastDedaoDuRefreshMs = now;
  qInfo() << "[SMART_REFRESH]" << m_tag << "dedao DU refresh"
          << "reason" << reason << "ghostingRisk" << m_ghostingRisk
          << "duCount" << m_duCount;
}

void SmartRefreshManager::scheduleDedaoDelayedRefresh(const QString &reason) {
  if (!(m_tag == QStringLiteral("dedao") && m_isBookPage)) {
    return;
  }
  m_dedaoDelayedRefresh.stop();
  m_dedaoDelayedRefresh.setProperty("reason", reason);
  const int delayMs = 50;
  m_dedaoDelayedRefresh.start(delayMs);
}

void SmartRefreshManager::scheduleDedaoFallbackSeries() {
  if (!(m_tag == QStringLiteral("dedao") && m_isBookPage)) {
    return;
  }
  cancelDedaoFallbacks();
  m_dedaoFallbackPending = true;
  m_dedaoFallbackShifted = false;
  m_dedaoFallback1s.start(1000);
  m_dedaoFallback2s.start(2000);
  m_dedaoFallback3s.start(3000);
}

void SmartRefreshManager::cancelDedaoFallbacks() {
  m_dedaoFallback1s.stop();
  m_dedaoFallback2s.stop();
  m_dedaoFallback3s.stop();
  m_dedaoFallbackPending = false;
  m_dedaoFallbackShifted = false;
}

void SmartRefreshManager::cancelDedaoDelayedRefresh() {
  m_dedaoDelayedRefresh.stop();
}

void SmartRefreshManager::startDedaoScrollSeries() {
  if (!(m_tag == QStringLiteral("dedao") && m_isBookPage)) {
    return;
  }
  m_dedaoScrollSeriesTimer.stop();
  m_dedaoScrollSeriesRemaining = kDedaoScrollSeriesCount;
  m_dedaoScrollSeriesTimer.start(kDedaoScrollSeriesIntervalMs);
  qInfo() << "[SMART_REFRESH]" << m_tag
          << "dedao scroll series scheduled"
          << "count" << m_dedaoScrollSeriesRemaining << "interval"
          << kDedaoScrollSeriesIntervalMs;
  shiftDedaoFallbackAfterScrollSeries();
}

void SmartRefreshManager::cancelDedaoScrollSeries() {
  if (!m_dedaoScrollSeriesTimer.isActive() &&
      m_dedaoScrollSeriesRemaining == 0) {
    return;
  }
  m_dedaoScrollSeriesTimer.stop();
  m_dedaoScrollSeriesRemaining = 0;
  if (m_tag == QStringLiteral("dedao")) {
    qInfo() << "[SMART_REFRESH]" << m_tag
            << "dedao scroll series canceled";
  }
}

void SmartRefreshManager::shiftDedaoFallbackAfterScrollSeries() {
  if (!m_dedaoFallbackPending || m_dedaoFallbackShifted) {
    return;
  }
  const bool anyActive = m_dedaoFallback1s.isActive() ||
                         m_dedaoFallback2s.isActive() ||
                         m_dedaoFallback3s.isActive();
  if (!anyActive) {
    m_dedaoFallbackPending = false;
    return;
  }
  const int baseDelay =
      kDedaoScrollSeriesIntervalMs * kDedaoScrollSeriesCount;
  if (m_dedaoFallback1s.isActive()) {
    m_dedaoFallback1s.start(baseDelay + 1000);
  }
  if (m_dedaoFallback2s.isActive()) {
    m_dedaoFallback2s.start(baseDelay + 2000);
  }
  if (m_dedaoFallback3s.isActive()) {
    m_dedaoFallback3s.start(baseDelay + 3000);
  }
  m_dedaoFallbackShifted = true;
  qInfo() << "[SMART_REFRESH]" << m_tag
          << "dedao fallback deferred after scroll series"
          << "baseDelay" << baseDelay;
}

void SmartRefreshManager::updateDedaoFallbackPending() {
  if (m_dedaoFallback1s.isActive() || m_dedaoFallback2s.isActive() ||
      m_dedaoFallback3s.isActive()) {
    return;
  }
  m_dedaoFallbackPending = false;
  m_dedaoFallbackShifted = false;
}

void SmartRefreshManager::processBatch() {
  if (m_eventQueue.isEmpty()) {
    return;
  }

  int totalScore = 0;
  int totalScrollDelta = 0;
  bool hasDOMChange = false;
  for (const auto &e : m_eventQueue) {
    totalScore += e.score;
    totalScrollDelta += e.scrollDelta;
    if (e.type == RefreshEvent::DOM_CHANGE) {
      hasDOMChange = true;
    }
  }
  qInfo() << "[SMART_REFRESH]" << m_tag << "processBatch"
          << "events" << m_eventQueue.size() << "domScore" << totalScore
          << "scrollDelta" << totalScrollDelta << "hasDom" << hasDOMChange
          << "book" << m_isBookPage << "clickPending" << m_clickPending
          << "clickRefreshCount" << m_clickRefreshCount << "lastScore"
          << m_lastRefreshScore;

  WaveformChoice wf = decideWaveform(m_eventQueue);
  QRect mergedRegion = mergeRegions();

  qInfo() << "[SMART_REFRESH]" << m_tag
          << "Decision: waveform=" << static_cast<int>(wf)
          << "region="
          << (mergedRegion.isNull() ? "fullscreen"
                                    : QString("%1,%2 %3x%4")
                                          .arg(mergedRegion.x())
                                          .arg(mergedRegion.y())
                                          .arg(mergedRegion.width())
                                          .arg(mergedRegion.height()));

  executeRefresh(wf, mergedRegion);

  if (wf != WF_NONE) {
    m_eventQueue.clear();
  } else {
    if (!m_isBookPage) {
      m_eventQueue.clear();
    }
  }
}

void SmartRefreshManager::onIdle() {
  if (m_ghostingRisk > 5.0f) {
    RefreshEvent e;
    e.type = RefreshEvent::IDLE;
    pushEvent(e);
  }
}

SmartRefreshManager::WaveformChoice
SmartRefreshManager::decideWaveform(const QVector<RefreshEvent> &events) {
  bool hasHighPriorityEvent = false;
  for (const auto &e : events) {
    if (e.type == RefreshEvent::PAGE_TURN) {
      return m_isBookPage ? WF_DU : WF_A2;
    }
    if (e.type == RefreshEvent::LOAD_FINISHED) {
      hasHighPriorityEvent = true;
      if (m_lastFullRefresh.elapsed() > 5000) {
        return WF_GC16_FULL;
      } else {
        return WF_GC16_PARTIAL;
      }
    }
    if (e.type == RefreshEvent::BURST_END) {
      hasHighPriorityEvent = true;
      return (m_ghostingRisk > 0.5f) ? WF_GC16_FULL : WF_GC16_PARTIAL;
    }
    if (e.type == RefreshEvent::MENU) {
      hasHighPriorityEvent = true;
      return WF_GC16_PARTIAL;
    }
    if (e.type == RefreshEvent::IDLE && m_ghostingRisk > 5.0f) {
      hasHighPriorityEvent = true;
      return WF_GC16_FULL;
    }
    if (e.type == RefreshEvent::CONTENT_READY) {
      hasHighPriorityEvent = true;
      m_lastRefreshScore = 0;
      m_clickPending = false;
      return WF_GC16_PARTIAL;
    }
  }

  int totalScore = 0;
  int totalScrollDelta = 0;
  bool hasDOMChange = false;
  for (const auto &e : events) {
    totalScore += e.score;
    totalScrollDelta += e.scrollDelta;
    if (e.type == RefreshEvent::DOM_CHANGE) {
      hasDOMChange = true;
    }
  }

  if (m_policy == PolicyReadingFirst) {
    const int absScroll = qAbs(totalScrollDelta);
    const int metric = qMax(totalScore, absScroll);
    if (m_isBookPage && hasDOMChange && !hasHighPriorityEvent) {
      const bool ignoreLastScoreGate =
          (m_tag == QStringLiteral("dedao"));
      bool allowSupplemental = false;
      if (m_clickPending) {
        m_clickPending = false;
      } else if (m_clickRefreshCount >= 0) {
        if (m_clickRefreshCount >= 2) {
          qInfo() << "[SMART_REFRESH]" << m_tag
                  << "Skip: click refresh limit reached (2)";
          return WF_NONE;
        }
        if (m_clickRefreshCount == 1) {
          if (totalScore <= kSupplementalDomScoreThreshold) {
            qInfo() << "[SMART_REFRESH]" << m_tag
                    << "Skip: supplemental refresh requires "
                       "score >"
                    << kSupplementalDomScoreThreshold << "score" << totalScore;
            return WF_NONE;
          }
          allowSupplemental = true;
        }
      }

      if (!ignoreLastScoreGate && !allowSupplemental &&
          m_lastRefreshScore > 0 &&
          totalScore <= m_lastRefreshScore) {
        qInfo() << "[SMART_REFRESH]" << m_tag << "Raw DOM score" << totalScore
                << "<= last refresh score" << m_lastRefreshScore
                << "(metric=" << metric
                << "), skipping refresh (requires larger score)";
        return WF_NONE;
      }
    }
    if (metric > 100)
      return WF_GC16_PARTIAL;
    if (metric > 10)
      return WF_DU;
    if (hasDOMChange || totalScrollDelta != 0) {
      qInfo() << "[SMART_REFRESH]" << m_tag
              << "Skip: metric <= 10 (reading policy) metric" << metric
              << "score" << totalScore << "scroll" << totalScrollDelta;
    }
    return WF_NONE;
  } else if (m_policy == PolicyInteractionFirst) {
    if (totalScrollDelta > 50)
      return WF_DU;
    if (totalScore > 100)
      return WF_GL16;
    if (totalScore > 20)
      return WF_DU;
    if (hasDOMChange || totalScrollDelta != 0) {
      qInfo() << "[SMART_REFRESH]" << m_tag
              << "Skip: low activity (interaction policy) score"
              << totalScore << "scroll" << totalScrollDelta;
    }
    return WF_NONE;
  }

  float riskMultiplier = 1.0f + m_ghostingRisk;
  if (totalScrollDelta > 200) {
    qint64 elapsed = m_lastRefreshTime.elapsed();
    return (elapsed < 200) ? WF_A2 : WF_GL16;
  }

  int adjustedScore = static_cast<int>(totalScore * riskMultiplier);
  if (adjustedScore > 300)
    return WF_GC16_PARTIAL;
  if (adjustedScore > 80)
    return WF_GL16;
  if (adjustedScore > 10)
    return WF_A2;

  if (hasDOMChange || totalScrollDelta != 0) {
    qInfo() << "[SMART_REFRESH]" << m_tag
            << "Skip: adjustedScore <= 10 score" << totalScore
            << "adjusted" << adjustedScore << "scroll" << totalScrollDelta
            << "ghostingRisk" << m_ghostingRisk;
  }
  return WF_NONE;
}

QRect SmartRefreshManager::mergeRegions() {
  int minX = INT_MAX, minY = INT_MAX, maxX = 0, maxY = 0;
  bool hasRegion = false;

  for (const auto &e : m_eventQueue) {
    if (!e.region.isNull()) {
      hasRegion = true;
      minX = qMin(minX, e.region.x());
      minY = qMin(minY, e.region.y());
      maxX = qMax(maxX, e.region.x() + e.region.width());
      maxY = qMax(maxY, e.region.y() + e.region.height());
    }
  }

  if (hasRegion && minX < INT_MAX) {
    const int padding = 10;
    minX = qMax(0, minX - padding);
    minY = qMax(0, minY - padding);
    maxX = qMin(m_width, maxX + padding);
    maxY = qMin(m_height, maxY + padding);
    return QRect(minX, minY, maxX - minX, maxY - minY);
  }

  return QRect();
}

void SmartRefreshManager::executeRefresh(WaveformChoice wf,
                                         const QRect &region) {
  if (m_postClickA2Pending) {
    schedulePostClickA2();
  }
  if (wf == WF_NONE) {
    if (!m_isBookPage) {
      m_eventQueue.clear();
    }
    return;
  }

  const bool isDedaoBook = (m_tag == QStringLiteral("dedao")) && m_isBookPage;
  const bool needFullCleanup =
      isDedaoBook ? (m_ghostingRisk >= 3.0f)
                  : (m_partialCount >= m_maxPartialBeforeCleanup) ||
                        (m_ghostingRisk >= 3.0f) ||
                        (m_duCount >= m_maxDUBeforeCleanup);

  if (needFullCleanup) {
    wf = WF_GC16_FULL;
  }

  bool a2ConvertedToDu = false;
  if (m_isBookPage && wf == WF_A2) {
    wf = WF_DU;
    a2ConvertedToDu = true;
  }

  bool isFullScreen = region.isNull() || (region.width() >= m_width * 0.8 &&
                                          region.height() >= m_height * 0.8);

  const char *wfStr = (wf == WF_A2)             ? "A2"
                      : (wf == WF_DU)           ? "DU"
                      : (wf == WF_GL16)         ? "GL16"
                      : (wf == WF_GC16_PARTIAL) ? "GC16_PARTIAL"
                      : (wf == WF_GC16_FULL)    ? "GC16_FULL"
                                                : "NONE";

  qInfo() << "[SMART_REFRESH]" << m_tag << "Execute" << wfStr << "region"
          << (isFullScreen ? "fullscreen"
                           : QString("%1,%2 %3x%4")
                                 .arg(region.x())
                                 .arg(region.y())
                                 .arg(region.width())
                                 .arg(region.height()))
          << "ghostingRisk" << m_ghostingRisk << "partialCount"
          << m_partialCount << "duCount" << m_duCount << "a2ToDu"
          << a2ConvertedToDu;

  if (m_isBookPage) {
    int currentScore = 0;
    bool hasDOMChange = false;
    for (const auto &e : m_eventQueue) {
      if (e.type == RefreshEvent::DOM_CHANGE) {
        currentScore += e.score;
        hasDOMChange = true;
      }
    }
    if (hasDOMChange && currentScore > 0) {
      m_lastRefreshScore = currentScore;
      if (m_clickRefreshCount >= 0) {
        m_clickRefreshCount = qMin(m_clickRefreshCount + 1, 2);
      }
    }
  }

  m_lastRefreshTime.restart();

  switch (wf) {
  case WF_GC16_FULL:
    m_fb->refreshFull(m_width, m_height);
    m_partialCount = 0;
    m_duCount = 0;
    m_ghostingRisk = 0.0f;
    m_lastFullRefresh.restart();
    break;
  case WF_GC16_PARTIAL:
    if (isFullScreen) {
      m_fb->refreshPartial(0, 0, m_width, m_height);
    } else {
      m_fb->refreshPartial(region.x(), region.y(), region.width(),
                           region.height());
    }
    m_partialCount++;
    m_ghostingRisk += 0.02f;
    break;
  case WF_GL16:
    m_fb->refreshScroll(m_width, m_height);
    m_partialCount++;
    m_ghostingRisk += 0.03f;
    break;
  case WF_A2:
    m_fb->refreshA2(0, 0, m_width, m_height);
    m_ghostingRisk += 0.05f;
    break;
  case WF_DU:
    m_fb->refreshUI(0, 0, m_width, m_height);
    m_duCount++;
    m_ghostingRisk += 0.01f;
    break;
  default:
    break;
  }
}

void SmartRefreshManager::schedulePostClickA2() {
  if (!m_postClickA2Enabled) {
    qInfo() << "[SMART_REFRESH]" << m_tag
            << "Post-click A2 disabled, skip schedule";
    return;
  }
  m_postClickA2Pending = false;
  m_postClickA2Count = 0;
  m_postClickA2Timer.stop();
  m_postClickA2Timer.start();
  const char *mode = m_isBookPage ? "DU" : "A2";
  qInfo() << "[SMART_REFRESH]" << m_tag
          << "Post-click refresh scheduled (mode" << mode << ", will do"
          << kMaxPostClickA2Count << "refreshes, 1s apart)";
}

void SmartRefreshManager::performPostClickA2() {
  if (!m_fb) {
    return;
  }
  if (!m_postClickA2Enabled) {
    m_postClickA2Pending = false;
    m_postClickA2Count = 0;
    m_postClickA2Timer.stop();
    return;
  }
  m_postClickA2Count++;
  const char *mode = m_isBookPage ? "DU" : "A2";
  qInfo() << "[SMART_REFRESH]" << m_tag << "Post-click refresh" << mode
          << m_postClickA2Count << "/" << kMaxPostClickA2Count;
  if (m_isBookPage) {
    m_fb->refreshUI(0, 0, m_width, m_height);
  } else {
    m_fb->refreshA2(0, 0, m_width, m_height);
  }

  if (m_postClickA2Count < kMaxPostClickA2Count) {
    m_postClickA2Timer.start();
    qInfo() << "[SMART_REFRESH]" << m_tag
            << "Next post-click A2 scheduled in"
            << kPostClickA2DelayMs << "ms";
  }
}
