#ifndef WEREAD_BROWSER_H
#define WEREAD_BROWSER_H

#include "catalog_widget.h"
#include <QAbstractSocket>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QNetworkCookie>
#include <QProcess>
#include <QPushButton>
#include <QRect>
#include <QStandardPaths>
#include <QString>
#include <QTcpSocket>
#include <QTimer>
#include <QTouchEvent>
#include <QUdpSocket>
#include <QUrl>
#include <QVariant>
#include <QWebEngineCookieStore>
#include <QWebEngineHistory>
#include <QWebEngineNavigationRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>
#include <QWheelEvent>
#include <QtGlobal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "common.h"
#include "eink_refresh.h"
#include "resource_interceptor.h"
#include "routed_page.h"
#include "smart_refresh.h"
#include "touch_logger.h"

static inline bool domScoreDebugEnabled() {
  if (!logLevelAtLeast(LogLevel::Info))
    return false;
  return qEnvironmentVariableIntValue("WEREAD_DOM_SCORE_DEBUG") != 0;
}

class WereadBrowser : public QMainWindow {
  Q_OBJECT
public:
  WereadBrowser(const QUrl &url, FbRefreshHelper *fbRef = nullptr);
private slots:
  void captureFrame();
  void onExitClicked();

public:
  CatalogWidget *catalogWidget() const;
  void injectMouse(Qt::MouseButton btn, QEvent::Type type, const QPointF &pos);
  void injectTouch(QEvent::Type type, const QPointF &pos, int id = 0);
  void armInjectedMousePassThrough(const QPointF &pos, int durationMs = 600);
  void armInjectedTouchPassThrough(const QPointF &pos, int durationMs = 600);
  bool shouldReplayInjectedMouse(const QPointF &pos, qint64 nowMs) const;
  void noteInjectedMouseReplayed(const QPointF &pos, qint64 nowMs);
  bool shouldBypassGestureForInjectedMouse(const QPointF &pos,
                                           qint64 nowMs) const;
  bool shouldBypassGestureForInjectedTouch(const QPointF &pos,
                                           qint64 nowMs) const;
  void injectKey(int key);
  // 获取内部 WebEngineView，用于安装事件过滤器（如 GestureFilter）
  QWebEngineView *view() const;
  void scrollByJs(int dy);
  int pageStep() const;
  void hittestJs(const QPointF &pos);
  void injectWheel(const QPointF &pos, int dy);
  void goNextPage(const QPointF &inputPos = QPointF());
  void goPrevPage(const QPointF &inputPos = QPointF());
  void openCatalog();
  bool handleMenuTap(const QPointF &globalPos);
  void openWeReadFontPanelAndSelect();
  void adjustFont(bool increase);
  void toggleTheme();
  void toggleFontFamily();
  void toggleService();
  void applyDefaultLightTheme();
  void dedaoScroll(bool forward);

  void fetchCatalog();

  void goBack();
  void goWeReadHome();
  void allowDedaoDetailOnce();
  void showMenu();
  void exitToXochitl(const QString &exitReason = QStringLiteral("unknown"));

  // 触发全黑覆盖 + 双次全屏清理刷新（INIT FULL → GC16 FULL）
  void triggerFullRefresh();

public slots:
  void sendBookState();
  void handleStateRequest();

private:
  void injectKeyWithModifiers(int key, Qt::KeyboardModifiers modifiers);

  void initProfileAndView(const QUrl &url);
  void initLoadSignals();
  void initNavigationSignals();
  void initStartupLoad(const QUrl &url);
  void initIdleCleanup();
  void initSessionAutoSave();
  void initMenuOverlay();
  void initStateResponder();

  QWebEngineView *m_view = nullptr; // Active view shown to user
  CatalogWidget *m_catalogWidget = nullptr;
  FbRefreshHelper *m_fbRef = nullptr;
  SmartRefreshManager *m_smartRefreshWeRead = nullptr; // 智能刷新管理器（微信读书）
  SmartRefreshManager *m_smartRefreshDedao = nullptr;  // 智能刷新管理器（得到）
  QTimer *m_idleCleanupTimer = nullptr;          // 空闲清理定时器
  QTimer *m_sessionSaveTimer = nullptr; // 定期保存会话状态定时器（每分钟）
  mutable qint64 m_lastSessionSaveTime =
      0; // 上次保存会话状态的时间戳（用于防抖，mutable
         // 允许在 const 方法中修改）
  // 已废弃：不再通过改 QTFB_KEY / 断开 qtfb
  // 连接来“交回系统”，而是走后台模式（停止刷新/渲染）
  QWebEngineProfile *m_profile = nullptr;
  // UA 控制：微信读书与得到分开配置，按页面类型切换
  QString m_uaNonWeRead;
  QString m_uaWeReadBook;
  QString m_uaDedaoBook;
  QString m_kindleUA;
  QString m_ipadMiniUA;
  QString m_desktopChromeUA; // 桌面版Chrome UA（用于微信读书首页）
  QString m_currentUA;
  QString m_weReadBookMode;
  QString m_uaMode = QStringLiteral("default");
  QString detectUaMode(const QString &ua) const;
  QFrame *m_menu = nullptr;
  QFrame *m_blackOverlay = nullptr;
  QTimer m_menuTimer;
  QTimer m_heartbeatTimer;
  bool m_disablePrep = true;   // 关闭首帧捕获/样式准备
  bool m_disableRescue = true; // 关闭救援/兜底
  int m_pageTurnCounter = 0; // 翻页计数（按节拍触发 keepalive ping）
  int m_pingInterval = 10;   // 默认每10次翻页 ping 一次
  bool m_pingDisabled = false;    // 环境变量 WEREAD_PING_DISABLE 可关闭
  int m_pingMaxRetries = 3;       // ping 失败重试次数
  int m_pingRetryDelayMs = 10000; // 重试间隔 10s
  int m_pingTimeoutMs = 8000;     // 单次 TCP 探测超时
  qint64 m_pingLastReloadMs = 0; // 最近一次因 ping 失败触发 reload 的时间戳
  int m_emptyReadyPolls = 0;
  bool m_enableAutoReload = false; // 旧的自动重载策略（已默认关闭）
  int m_jsUnexpectedCount = 0;
  qint64 m_jsLastUnexpectedMs = 0;
  qint64 m_jsUnexpectedLastLogMs = 0;
  qint64 m_jsReloadCooldownMs = 0;
  bool m_stallRescueTriggered = false;
  bool m_firstFrameDone = false;
  int m_reloadAttempts = 0;
  int m_integrityReloads = 0;
  int m_bookEarlyReloads = 0;
  bool m_enableHighWhiteRescue = false; // 可开关的高白救援（默认暂停）
  bool m_highWhiteRescueDone = false;
  bool m_diagRan = false;
  bool m_contentReadyTriggered = false; // 内容准备好事件是否已触发
  // 智能延迟重试机制（低功耗）
  int m_contentRetryCount = 0;       // 内容重试计数器
  qint64 m_lastContentCheckTime = 0; // 上次内容检查时间
  qint64 m_lastContentRetryTime = 0; // 上次内容重试时间
  bool m_contentCheckScheduled = false; // 内容检查是否已调度（避免重复调度）
  bool m_contentFirstCheckFailed = false; // 第一次检查是否失败（用于二次确认）
  static constexpr int MAX_CONTENT_RETRIES = 2; // 最大重试次数（减少到2次）
  static constexpr int CONTENT_CHECK_DELAY_MS =
      15000; // 首次检查延迟（15秒，给内容足够时间加载）
  static constexpr int CONTENT_RETRY_INTERVAL_MS = 15000; // 重试间隔（15秒）
  static constexpr int CONTENT_CHECK_COOLDOWN_MS = 30000; // 检查冷却期（30秒）
  static constexpr int CONTENT_CONFIRM_DELAY_MS =
      10000; // 二次确认延迟（10秒，避免误判）
  bool m_scriptInventoryLogged = false;
  bool m_resourceLogged = false;
  bool m_textScanDone = false;
  bool m_fontLogged = false;
  bool m_defaultSettingsScriptInstalled = false;
  bool m_dedaoDefaultSettingsScriptInstalled = false;
  bool m_chapterObserverScriptInstalled = false;
  int m_rescueSeq = 0;
  bool m_iframeFixApplied = false;
  bool m_lowPowerCssInjected = false;
  bool m_allowDedaoDetailOnce = false;
  QUdpSocket m_stateSender;
  QUdpSocket m_stateResponder;
  QUrl currentUrl;
  QString m_prevUrlStr;
  QString m_lastNavReason;
  QUrl m_lastNavReasonTarget;
  qint64 m_lastNavReasonTs = 0;
  QString m_lastDedaoReaderUrl;
  bool m_isBookPage = false;
  bool m_prevIsBookPage = false;
  qint64 m_lastReloadTs = 0;
  qint64 m_bookEnterTs = 0;
  qint64 m_lastChapterInfosRefreshMs = 0;
  quint64 m_catalogClickSeq = 0;
  bool m_dedaoProbed = false;
  bool m_dedaoStyleLogged = false;
  bool m_dedaoCatalogScanning = false;
  int m_dedaoCatalogScanRounds = 0;
  int m_dedaoCatalogScanStepPx = 0;
  int m_dedaoCatalogMaxScrollHeight = 0;
  int m_dedaoCatalogStableScrollHeightRounds = 0;
  QString m_dedaoCatalogScanKey;
  QString m_dedaoCatalogCacheKey;
  QJsonArray m_dedaoCatalogCache;
  bool m_dedaoCatalogJumpPending = false;
  bool m_navigating = false; // 翻页防抖锁
  int m_navSequence = 0;     // 翻页序列号，防止僵尸回调
  qint64 m_navStartTime = 0; // 翻页开始时间（用于检测慢速翻页）
  int m_pendingNavSeq = 0; // 待处理的翻页序列号（用于取消机制）
  QTimer *m_navTimeoutTimer = nullptr; // 翻页超时定时器（可取消）
  int m_pendingInputFallbackSeq = 0;   // 输入注入等待fallback的序列号
  qint64 m_inputFallbackStartMs = 0;   // 输入注入起始时间
  bool m_inputDomObserved = false;   // 输入注入后是否观察到DOM变化
  qint64 m_lastDomEventMs = 0;       // 最近一次DOM事件时间
  int m_lastDomEventScore = 0;       // 最近一次DOM事件分数
  qint64 m_lastMenuTapMs = 0;
  QPointF m_lastMenuTapPos;
  qint64 m_lastServiceToggleMs = 0;
  int m_lastPageTurnSeqNotified = 0; // 翻页节拍是否已记录
  int m_lastPageTurnEffectSeq = 0;   // 最近一次翻页生效来源记录
  qint64 m_lastTouchTs = 0;
  QPointF m_injectedMousePos;
  qint64 m_injectedMouseAllowUntilMs = 0;
  QPointF m_injectedMouseReplayPos;
  qint64 m_injectedMouseReplayUntilMs = 0;
  QPointF m_injectedTouchPos;
  qint64 m_injectedTouchAllowUntilMs = 0;
  // Font adjustment debounce (prevent duplicate executions)
  qint64 m_lastFontAdjustTime = 0;
  bool m_isAdjustingFont = false;

public:
  QString navReason() const { return m_lastNavReason; }
  QUrl navReasonTarget() const { return m_lastNavReasonTarget; }
  qint64 navReasonTs() const { return m_lastNavReasonTs; }
  QUrl currentUrlForLog() const { return currentUrl; }

  // 会话恢复相关方法（public，供 main() 调用）
  static QString getSessionStatePath();
  static QString getExitReasonPath();
  static void saveExitReason(const QString &reason);
  static QString loadExitReason();
  void saveSessionState() const;
  QUrl loadSessionUrl() const;
  int loadSessionScrollPosition() const;
  int m_restoredScrollY = 0; // 恢复的滚动位置（public，供 main() 访问）
  bool m_isRestoringSession =
      false; // 是否正在恢复会话（public，供 main() 访问）
  // 以下方法供外部类（如 GestureFilter）调用，需要保持 public
  bool isWeReadBook() const;
  bool isKindleUA() const;
  bool isWeReadKindleMode() const;
  bool isDedaoBook() const;
  bool isDedaoSite() const;
  QWebEngineView *getView() const;
  SmartRefreshManager *getSmartRefresh() const;
  SmartRefreshManager *getSmartRefreshWeRead() const;
  SmartRefreshManager *getSmartRefreshDedao() const;
  SmartRefreshManager *smartRefreshForUrl(const QUrl &url) const;
  SmartRefreshManager *smartRefreshForPage() const;
  void handleSmartRefreshEvents(const QString &json);
  void handleSmartRefreshBurstEnd();
  void scheduleBookCaptures(bool force = false);
  void restartCaptureLoop();
  void openDedaoMenu();
  void updateLastTouchTs(qint64 ts);

private:
  void forceRepaintNudge();
  void logPageTurnEffect(int currentSeq, const QString &source,
                         const QString &trigger, int score);
  QPointF resolveInputPos(const QPointF &inputPos, bool forward) const;
  void markPageTurnTriggered(int currentSeq, const QString &source);
  void scheduleInputFallback(bool forward, int currentSeq);
  void runWeReadPagerJsClick(bool forward, int currentSeq,
                             const QString &trigger);
  void noteDomEventFromJson(const QString &json);
  // 根据 URL 切换 UA：微信书籍页用 Qt 默认 UA，其余用配置的 iOS/Kindle/Android
  // UA
  void updateUserAgentForUrl(const QUrl &url);
  void installWeReadDefaultSettingsScript();
  void installDedaoDefaultSettingsScript();
  void ensureDedaoDefaults();
  void scheduleCapture(int delayMs = 100);
  // 仅针对微信读书书籍页：对齐滚动到行高整数倍并添加轻微留白，避免半截行
  void alignWeReadPagination();
  // 【视觉诊断】正文有字但不显示时，采集容器的可见性/尺寸/遮挡信息
  void runVisualDiagnostic();
  // 应用书籍页样式修复和视觉诊断（从 Ready 机制中提取）
  void applyBookPageFixes();
  void applyWeReadFontOverrideIfNeeded();
  void applyDedaoFontOverrideIfNeeded();
  void scheduleDedaoUiFixes();
  void runDedaoUiFixes();
  void scheduleWeReadUiFixes();
  void runWeReadUiFixes();
  void openDedaoCatalog();
  void fetchDedaoCatalog();
  void handleDedaoCatalogClick(int index, const QString &uid);
  void advanceDedaoCatalogScan(bool reset);
  void scheduleDedaoCatalogScrape(int delayMs);
  void hideDedaoNativeCatalog();
  void recordNavReason(const QString &reason, const QUrl &target);

  void handleJsUnexpected(const QString &message, const QString &source);

private:
  void updateHeartbeat();
  void cancelPendingCaptures();
  qint64 m_lastKeepaliveNoStateLogMs = 0;

  // 智能延迟重试：检查内容并重试（低功耗）
  void checkContentAndRetryIfNeeded();

  void keepAliveNearChapterEnd();

  // 带重试逻辑的内容检查（用于智能延迟重试）
  void keepAliveNearChapterEndWithRetry();

  // 翻页节拍：每 N 次翻页发送一次 ping（独立于 idx/章节状态）
  void onPageTurnEvent();

  // 进程内网络探针：使用 Qt 网络层，绕过页面环境
  void sendKeepAlivePing(int triggerCount, int attempt);

  void maybeReloadAfterPingFail(int triggerCount);

  void runDomDiagnostic();
  void runDedaoProbe();
  void installChapterObserver();
  void installChapterObserverScript();
  void installSmartRefreshScript(const QString &flagName);
  QString buildSmartRefreshScript(const QString &flagName) const;

public:
  void cycleUserAgentMode();
  // 无需updateUaButtonText，已删除UA按钮
  void logUrlState(const QString &tag = QString());
  void logHistoryState(const QString &tag = QString());

  // 简易探针：记录当前 URL、滚动容器和主要正文节点
  void logPageProbe(const QString &tag = QString());

  void installWeReadFontClampScript();

  // 记录当前页面脚本清单，辅助检查脚本截断/重复加载
  void logScriptInventory(bool once = true);

  // 记录性能资源条目，关注 app/js/css 是否加载、大小
  void logResourceEntries(bool once = true);

  void logComputedFontOnce();

  // 全量扫描文本长度/画布/图片数量，用于判断非 DOM 文本渲染
  void logTextScan(bool once = true);

  // 微信读书：滚动替代分页，步长为屏高 0.8，滚动后对齐行高
  void weReadScroll(bool forward);
  void triggerStalledRescue(const QString &reason, int chapterLen, int bodyLen,
                            int pollCount);

  // Script accessors (implemented in weread_browser_scripts.cpp)
  static const QString &getChapterObserverScript();
  static const QString &getDefaultSettingsScript();
  static const QString &getDedaoDefaultSettingsScript();
};

#endif // WEREAD_BROWSER_H
