#ifndef GESTURE_FILTER_H
#define GESTURE_FILTER_H

#include <QElapsedTimer>
#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QPointF>
#include <QTimer>
#include <QTouchEvent>
#include <QWidget>

#include "weread_browser.h"

// KOReader 风格手势检测器：状态机驱动，区分 tap/pan/swipe/hold
// 参考: weread-test/koreader/frontend/device/gesturedetector.lua
class GestureFilter : public QObject {
  Q_OBJECT
public:
  // 手势状态机状态 (参考 KOReader Contact 状态)
  enum GestureState {
    StateIdle, // 空闲，等待触摸
    StateTap,  // 触摸开始，等待判定 tap/pan/hold
    StatePan,  // 持续移动中（实时滚动）
    StateHold  // 长按状态
  };

  // 手势方向 (参考 KOReader getPath())
  enum Direction {
    DirNone,
    DirNorth, // 上
    DirSouth, // 下
    DirEast,  // 右
    DirWest   // 左
  };

  explicit GestureFilter(WereadBrowser *browser, QObject *parent = nullptr);

  void setWindowHeight(int height);

protected:
  bool eventFilter(QObject *obj, QEvent *ev) override;

private slots:
  void onHoldTimeout();

private:
  // ==================== KOReader 风格参数 ====================
  // 时间参数 (ms) - 参考 gesturedetector.lua
  static constexpr qint64 TAP_INTERVAL_MS = 0; // tap 防抖（我们不需要）
  static constexpr qint64 DOUBLE_TAP_INTERVAL_MS = 300; // 双击间隔
  static constexpr qint64 HOLD_INTERVAL_MS = 500;       // 长按阈值
  static constexpr qint64 SWIPE_INTERVAL_MS = 900; // 滑动必须在此时间内完成
  static constexpr qint64 DUPLICATE_EVENT_MS = 120; // 短时间内重复事件判定
  static constexpr qint64 SYNTH_MOUSE_GUARD_MS =
      200; // 近期有 Touch 时吞掉合成鼠标

  // 距离参数 (像素) - 参考 gesturedetector.lua scaleByDPI
  // reMarkable Paper Pro DPI ≈ 226, 35 DPI ≈ 35 * (226/160) ≈ 49 px
  static constexpr qreal PAN_THRESHOLD = 50.0;       // pan 触发阈值
  static constexpr qreal TAP_BOUNCE_DISTANCE = 40.0; // tap 抖动容差
  static constexpr qreal SWIPE_MIN_DISTANCE = 100.0; // swipe 最小距离
  static constexpr qreal LEFT_EDGE_SWIPE_THRESHOLD = 100.0; // 左侧边缘阈值
  static constexpr qreal DUPLICATE_POS_TOL = 12.0;   // 重复事件位置容差
  static constexpr qreal SYNTH_MOUSE_POS_TOL = 20.0; // 合成鼠标匹配触摸位置容差

  // ==================== 状态机核心处理 ====================

  // 触摸开始 (参考 KOReader Contact:initialState -> tapState)
  bool handleContactDown(const QPointF &pos);

  // 触摸移动 (参考 KOReader Contact:handleNonTap -> panState)
  // isBookPage: true=书籍页面（处理滚动），false=非书籍页面（只记录状态）
  bool handleContactMove(const QPointF &pos, bool isBookPage = true);

  // 触摸结束 (参考 KOReader tapState/panState 的 contact lift 处理)
  // isBookPage:
  // true=书籍页面（处理所有手势），false=非书籍页面（只处理底部向上滑动退出）
  bool handleContactUp(const QPointF &pos, bool isBookPage = true);

  // ==================== 微信读书专用手势处理 ====================

  // WeRead-specific gesture handlers
  bool handleWeReadContactDown(const QPointF &pos);
  bool handleWeReadContactMove(const QPointF &pos);
  bool handleWeReadContactUp(const QPointF &pos);

  // ==================== 得到专用手势处理 ====================

  // Dedao-specific gesture handlers
  bool handleDedaoContactDown(const QPointF &pos);
  bool handleDedaoContactMove(const QPointF &pos);
  bool handleDedaoContactUp(const QPointF &pos);

  // ==================== 全局手势处理 ====================

  // Handle global gestures (bottom swipe up, right edge swipe left)
  bool handleGlobalGestures(const QPointF &startPos, const QPointF &endPos,
                            qint64 durationMs, GestureState state);

  // ==================== 辅助函数 ====================

  // 计算方向 (参考 KOReader Contact:getPath)
  Direction getDirection(qreal dx, qreal dy) const;

  // 方向名称（调试用）
  static const char *directionName(Direction dir);

  static bool isSamePosition(const QPointF &a, const QPointF &b, qreal tol);

  bool isDuplicatePointerEvent(QEvent::Type type, const QPointF &pos,
                               qint64 nowMs) const;

  void recordPointerEvent(QEvent::Type type, const QPointF &pos, qint64 nowMs);

  void noteTouchEvent(const QPointF &pos, qint64 nowMs);

  bool hasRecentTouch(const QPointF &pos, qint64 nowMs) const;
  bool isCenterZone(const QPointF &pos) const;

  // ==================== 成员变量 ====================
  WereadBrowser *m_browser = nullptr;
  int m_windowHeight = 1696; // 窗口高度（用于判断底部区域）

  // 状态机
  GestureState m_state = StateIdle;

  // 触摸坐标
  QPointF m_startPos;       // 触摸起始点
  QPointF m_currentPos;     // 当前位置
  QPointF m_panStartPos;    // pan 开始位置
  QPointF m_lastPanPos;     // 上次 pan 位置（用于增量滚动）
  QPointF m_panAccumulated; // pan 累计位移

  // 定时器
  QElapsedTimer m_timer; // 手势时长计时
  QTimer m_holdTimer;    // 长按定时器
  bool m_verboseLogs = false;
  QElapsedTimer m_eventClock;
  qint64 m_lastTouchMs = -1;
  QPointF m_lastTouchPos;
  QEvent::Type m_lastPointerType = QEvent::None;
  QPointF m_lastPointerPos;
  qint64 m_lastPointerMs = -1;
  bool m_dedaoPanSuppressScroll = false;
  bool m_holdPassThroughCandidate = false;
  bool m_holdPassThroughActive = false;
  bool m_holdPassThroughInjected = false;
  QPointF m_holdPassThroughPos;
};

// GlobalInputLogger 已移除：所有功能已由 GestureFilter 统一处理
// - 菜单检查：GestureFilter 已实现
// - 滚动刷新：GestureFilter 通过 scrollByJs() 触发，由 SmartRefreshManager 处理

#endif // GESTURE_FILTER_H
