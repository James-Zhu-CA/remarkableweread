#ifndef SMART_REFRESH_H
#define SMART_REFRESH_H

#include "eink_refresh.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QVector>

// ============================================================================
// 智能刷新管理器 (SmartRefreshManager)
// 三层架构：DOM 监听 → 事件队列 → 决策引擎 → 刷新执行
// 参考：KOReader 刷新策略、用户优化建议
// ============================================================================
class SmartRefreshManager : public QObject {
  Q_OBJECT
public:
  // 三档策略模式
  enum RefreshPolicy {
    PolicyReadingFirst,     // 阅读优先：多GC16，清晰
    PolicyInteractionFirst, // 交互优先：多A2，流畅
    PolicySmartBalance      // 智能平衡：自动适应（默认）
  };

  // 波形选择
  enum WaveformChoice {
    WF_NONE,
    WF_A2,
    WF_DU,
    WF_GL16,
    WF_GC16_PARTIAL,
    WF_GC16_FULL
  };

  // 刷新事件类型
  struct RefreshEvent {
    enum Type {
      DOM_CHANGE,    // DOM 变化
      SCROLL,        // 滚动
      PAGE_TURN,     // 翻页
      MENU,          // 菜单
      LOAD_FINISHED, // 加载完成
      BURST_END,     // 猝发结束
      IDLE,          // 空闲
      CONTENT_READY  // 内容准备好
    };
    Type type = DOM_CHANGE;
    int score = 0;       // DOM 变化分数
    int scrollDelta = 0; // 滚动距离
    QRect region;        // 变化区域（可选）
  };

  explicit SmartRefreshManager(FbRefreshHelper *fb, int w, int h,
                               const QString &tag = QString(),
                               QObject *parent = nullptr);

  QString tag() const { return m_tag; }

  void setPolicy(RefreshPolicy policy) { m_policy = policy; }
  RefreshPolicy policy() const { return m_policy; }

  void setPostClickA2Enabled(bool enabled);

  void setBookPage(bool isBook);
  void resetScoreThreshold();
  void pushEvent(const RefreshEvent &event);
  void parseJsEvents(const QString &json);

  // 直接触发特定类型事件
  void triggerPageTurn();
  void triggerLoadFinished();
  void triggerMenu();
  void triggerBurstEnd();
  void triggerContentReady();
  void triggerDedaoDuRefresh(const QString &reason);
  void scheduleDedaoFallbackSeries();
  void cancelDedaoFallbacks();
  void scheduleDedaoDelayedRefresh(const QString &reason);
  void cancelDedaoDelayedRefresh();

  // 获取状态
  float ghostingRisk() const { return m_ghostingRisk; }
  int partialCount() const { return m_partialCount; }
  int duCount() const { return m_duCount; }

private slots:
  void processBatch();
  void onIdle();
  void performPostClickA2();

private:
  WaveformChoice decideWaveform(const QVector<RefreshEvent> &events);
  QRect mergeRegions();
  void executeRefresh(WaveformChoice wf, const QRect &region);
  void schedulePostClickA2();
  void startDedaoScrollSeries();
  void cancelDedaoScrollSeries();
  void shiftDedaoFallbackAfterScrollSeries();
  void updateDedaoFallbackPending();

  FbRefreshHelper *m_fb;
  int m_width, m_height;
  RefreshPolicy m_policy = PolicySmartBalance;

  // 事件队列
  QVector<RefreshEvent> m_eventQueue;
  QTimer m_batchTimer;
  QTimer m_idleTimer;

  // 状态跟踪
  int m_partialCount = 0;
  int m_duCount = 0;
  float m_ghostingRisk = 0.0f;
  QElapsedTimer m_lastFullRefresh;
  QElapsedTimer m_lastRefreshTime;
  QElapsedTimer m_lastActivityTime;

  // 动态阈值
  int m_maxPartialBeforeCleanup = 15;
  int m_maxDUBeforeCleanup = 25;

  // 递增阈值机制
  bool m_isBookPage = false;
  int m_lastRefreshScore = 0;
  bool m_clickPending = false;
  int m_clickRefreshCount = -1;
  static constexpr int kSupplementalDomScoreThreshold = 300;

  // 点击翻页后的延迟 A2 刷新
  bool m_postClickA2Pending = false;
  bool m_postClickA2Enabled = true;
  int m_postClickA2Count = 0;
  QTimer m_postClickA2Timer;
  static constexpr int kPostClickA2DelayMs = 1000;
  static constexpr int kMaxPostClickA2Count = 10;
  static constexpr int kDedaoDuThrottleMs = 250;
  static constexpr int kDedaoScrollSeriesIntervalMs = 1000;
  static constexpr int kDedaoScrollSeriesCount = 5;
  qint64 m_lastDedaoDuRefreshMs = 0;
  QTimer m_dedaoFallback1s;
  QTimer m_dedaoFallback2s;
  QTimer m_dedaoFallback3s;
  QTimer m_dedaoDelayedRefresh;
  QTimer m_dedaoIdleRefresh;
  QTimer m_dedaoMutationRefresh;
  QTimer m_dedaoScrollSeriesTimer;
  int m_dedaoScrollSeriesRemaining = 0;
  int m_lastDedaoClickSeq = -1;
  bool m_dedaoClickHandled = false;
  bool m_dedaoBypassThrottleOnce = false;
  bool m_dedaoFallbackPending = false;
  bool m_dedaoFallbackShifted = false;

  QString m_tag;
};

#endif // SMART_REFRESH_H
