#include "gesture_filter.h"
#include <QCoreApplication>
#include <QDateTime>

namespace {
const char *eventTypeName(QEvent::Type type) {
  switch (type) {
  case QEvent::MouseButtonPress:
    return "MousePress";
  case QEvent::MouseButtonRelease:
    return "MouseRelease";
  case QEvent::MouseMove:
    return "MouseMove";
  case QEvent::TouchBegin:
    return "TouchBegin";
  case QEvent::TouchUpdate:
    return "TouchUpdate";
  case QEvent::TouchEnd:
    return "TouchEnd";
  default:
    return "Other";
  }
}

bool isTouchEventType(QEvent::Type type) {
  return type == QEvent::TouchBegin || type == QEvent::TouchUpdate ||
         type == QEvent::TouchEnd;
}

bool isMouseEventType(QEvent::Type type) {
  return type == QEvent::MouseButtonPress ||
         type == QEvent::MouseButtonRelease || type == QEvent::MouseMove ||
         type == QEvent::MouseButtonDblClick;
}

bool isPointerEventType(QEvent::Type type) {
  return isTouchEventType(type) || isMouseEventType(type);
}

bool extractEventPos(QEvent *ev, QPointF *pos) {
  if (!ev || !pos)
    return false;
  switch (ev->type()) {
  case QEvent::MouseButtonPress:
  case QEvent::MouseButtonRelease:
  case QEvent::MouseMove:
  case QEvent::MouseButtonDblClick: {
    auto *me = static_cast<QMouseEvent *>(ev);
    *pos = me->position();
    return true;
  }
  case QEvent::TouchBegin:
  case QEvent::TouchUpdate:
  case QEvent::TouchEnd: {
    auto *te = static_cast<QTouchEvent *>(ev);
    if (te->points().isEmpty())
      return false;
    *pos = te->points().first().position();
    return true;
  }
  default:
    return false;
  }
}
} // namespace



GestureFilter::GestureFilter(WereadBrowser *browser, QObject *parent ) : QObject(parent), m_browser(browser), m_windowHeight(1696) {
    // Hold 定时器：500ms 后触发长按 (参考 KOReader HOLD_INTERVAL_MS = 500)
    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(HOLD_INTERVAL_MS);
    connect(&m_holdTimer, &QTimer::timeout, this,
            &GestureFilter::onHoldTimeout);
    m_verboseLogs = logLevelAtLeast(LogLevel::Info);
    m_eventClock.start();
  }

void GestureFilter::setWindowHeight(int height) {
    m_windowHeight = height;
    qInfo() << "[GESTURE] Window height set to" << m_windowHeight;
  }

bool GestureFilter::eventFilter(QObject *obj, QEvent *ev) {
    if (!m_browser) {
      qWarning() << "[GESTURE_DEBUG] eventFilter: m_browser is null, event type"
                 << ev->type();
      return QObject::eventFilter(obj, ev);
    }

    const QEvent::Type type = ev->type();

    // 只处理 QWidgetWindow 的事件，忽略 QQuickWidget 的事件，避免重复处理
    // QWidgetWindow 是顶层窗口，事件会先到达这里；QQuickWidget
    // 是视图内部组件，事件会再次到达
    const char *objClassName = obj ? obj->metaObject()->className() : nullptr;
    if (objClassName &&
        QString::fromLatin1(objClassName) == QStringLiteral("QQuickWidget")) {
      if (m_verboseLogs && isPointerEventType(type)) {
        qInfo() << "[GESTURE_DECISION] bypass QQuickWidget event"
                << eventTypeName(type);
      }
      // 对于 QQuickWidget，直接返回 false，不拦截，让事件继续传递
      // 这样可以避免同一个事件被处理两次（一次在 QWidgetWindow，一次在
      // QQuickWidget）
      return false;
    }

    const qint64 nowMs = m_eventClock.isValid() ? m_eventClock.elapsed() : 0;
    const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();

    if (isTouchEventType(type)) {
      auto *te = static_cast<QTouchEvent *>(ev);
      if (!te->points().isEmpty()) {
        const QPointF pos = te->points().first().position();
        if (m_browser &&
            m_browser->shouldBypassGestureForInjectedTouch(pos, nowWallMs)) {
          if (m_verboseLogs) {
            qInfo() << "[GESTURE] pass injected touch event type" << ev->type()
                    << "pos" << pos;
          }
          return false;
        }
      }
    }

    // 忽略我们自己注入的鼠标事件，避免被手势逻辑再次处理
    if (isMouseEventType(type)) {
      auto *me = static_cast<QMouseEvent *>(ev);
      if (me->source() == Qt::MouseEventSynthesizedByApplication) {
        if (m_browser &&
            m_browser->shouldReplayInjectedMouse(me->position(), nowWallMs)) {
          if (m_verboseLogs) {
            qInfo() << "[GESTURE] replay injected mouse event type"
                    << ev->type() << "pos" << me->position();
          }
          m_browser->noteInjectedMouseReplayed(me->position(), nowWallMs);
          if (obj) {
            auto *replay = new QMouseEvent(
                ev->type(), me->position(), me->position(),
                me->globalPosition(), me->button(), me->buttons(),
                me->modifiers(), Qt::MouseEventNotSynthesized);
            QCoreApplication::postEvent(obj, replay);
          }
          return true;
        }
        if (m_verboseLogs) {
          qInfo() << "[GESTURE] skip injected mouse event type" << ev->type()
                  << "pos" << me->position();
        }
        return false;
      }
    }

    if (isMouseEventType(type)) {
      auto *me = static_cast<QMouseEvent *>(ev);
      if (m_browser &&
          m_browser->shouldBypassGestureForInjectedMouse(me->position(),
                                                         nowWallMs)) {
        if (m_verboseLogs) {
          qInfo() << "[GESTURE] pass injected mouse event type" << ev->type()
                  << "pos" << me->position();
        }
        return false;
      }
    }

    if (isTouchEventType(type)) {
      auto *te = static_cast<QTouchEvent *>(ev);
      if (!te->points().isEmpty()) {
        noteTouchEvent(te->points().first().position(), nowMs);
      }
    }

    // 检查事件目标是否是菜单或菜单的子控件 - 如果是，拦截事件，防止传递到页面
    QWidget *widget = qobject_cast<QWidget *>(obj);
    if (widget) {
      // 通过对象名检查：菜单设置了 objectName="weread_menu_overlay"
      // 或者是 CatalogWidget
      QWidget *check = widget;
      while (check) {
        if (check->objectName() == QStringLiteral("weread_menu_overlay") ||
            check->objectName() == QStringLiteral("CatalogWidget")) {
          // 对于触摸和鼠标事件，拦截它们，防止传递到页面导致翻页
          // 注意：返回 false
          // 让事件正常传递给按钮，但不会传递到页面（因为控件在页面上层）
          if (isPointerEventType(type)) {
            qInfo() << "[GESTURE] Event on overlay widget, allowing to widget"
                       " (not to page), obj="
                    << obj->metaObject()->className()
                    << "widgetName=" << check->objectName()
                    << "type=" << ev->type();
            // 返回 false 让事件传递给控件
            return false;
          }
          return false;
        }
        check = check->parentWidget();
      }
    }

    // 额外检查：如果 CatalogWidget 可见，且触摸位置在其区域内，拦截事件
    if (m_browser && m_browser->catalogWidget() &&
        m_browser->catalogWidget()->isVisible()) {
      QPointF touchPos;
      bool hasPos = false;
      if (isTouchEventType(type)) {
        auto *te = static_cast<QTouchEvent *>(ev);
        if (!te->points().isEmpty()) {
          touchPos = te->points().first().position();
          hasPos = true;
        }
      } else if (isMouseEventType(type)) {
        auto *me = static_cast<QMouseEvent *>(ev);
        touchPos = me->position();
        hasPos = true;
      }

      if (hasPos) {
        QRect catalogRect = m_browser->catalogWidget()->geometry();
        if (catalogRect.contains(touchPos.toPoint())) {
          qInfo()
              << "[GESTURE] Touch in CatalogWidget area, forwarding to catalog"
              << "pos=" << touchPos << "catalogRect=" << catalogRect;
          // 让事件传递给 CatalogWidget
          return false;
        }
      }
    }

    if (m_browser && (type == QEvent::TouchEnd ||
                      type == QEvent::MouseButtonRelease)) {
      QPointF globalPos;
      QPointF localPos;
      bool hasPos = false;
      if (type == QEvent::TouchEnd) {
        auto *te = static_cast<QTouchEvent *>(ev);
        if (!te->points().isEmpty()) {
          const auto &pt = te->points().first();
          globalPos = pt.globalPosition();
          localPos = pt.position();
          hasPos = true;
        }
      } else {
        auto *me = static_cast<QMouseEvent *>(ev);
        globalPos = me->globalPosition();
        localPos = me->position();
        hasPos = true;
      }
      if (hasPos && m_browser->handleMenuTap(globalPos)) {
        qInfo() << "[MENU] fallback tap handled pos" << localPos;
        if (m_verboseLogs) {
          qInfo() << "[GESTURE_DECISION] menu tap handled" << localPos
                  << "type" << eventTypeName(type);
        }
        return true;
      }
    }

    // 检查是否在书籍页面
    const bool isWeReadBook = m_browser->isWeReadBook();
    const bool isDedaoBook = m_browser->isDedaoBook();
    const bool isBookPage = isWeReadBook || isDedaoBook;

    // 书籍页面：近期有 Touch 时吞掉系统/Qt
    // 合成的鼠标事件，避免触摸+鼠标重复触发手势
    if (isMouseEventType(type)) {
      auto *me = static_cast<QMouseEvent *>(ev);
      if (me->source() == Qt::MouseEventSynthesizedBySystem ||
          me->source() == Qt::MouseEventSynthesizedByQt) {
        if (isBookPage && hasRecentTouch(me->position(), nowMs)) {
          if (m_verboseLogs) {
            qInfo() << "[GESTURE] skip synthesized mouse event type"
                    << ev->type() << "pos" << me->position();
          }
          return true;
        }
      }
    }

    // 如果事件不是发生在菜单控件上，但在书籍页面，检查事件位置是否在菜单区域内
    // 如果位置在菜单区域内且菜单可见，让事件传递给菜单（不拦截）
    // 如果位置在菜单区域内但菜单不可见，拦截事件防止页面翻页
    if (isBookPage && !widget &&
        (type == QEvent::TouchBegin || type == QEvent::TouchEnd ||
         type == QEvent::MouseButtonPress ||
         type == QEvent::MouseButtonRelease)) {
      QPointF pos;
      bool hasPos = false;
      if (type == QEvent::TouchBegin || type == QEvent::TouchEnd) {
        auto *te = static_cast<QTouchEvent *>(ev);
        if (!te->points().isEmpty()) {
          pos = te->points().first().position();
          hasPos = true;
        }
      } else {
        auto *me = static_cast<QMouseEvent *>(ev);
        pos = me->position();
        hasPos = true;
      }

      // 检查点击位置是否在菜单区域内（菜单在 y=70, height=100）
      if (hasPos && pos.y() >= 70 && pos.y() <= 170) {
        // 如果位置在菜单区域内，不拦截事件，让 Qt 事件系统正常处理
        // 如果菜单可见，事件会被菜单接收；如果菜单不可见，事件可能会传递到页面
        // 但由于菜单在页面上层（通过
        // raise()），如果菜单可见，事件应该会被菜单接收
        qInfo() << "[GESTURE] Touch/click in menu area, allowing event to pass "
                   "(menu will receive if visible), pos="
                << pos;
        return false; // 不拦截，让事件正常传递（菜单在页面上层，会优先接收）
      }
    }

    // 底部向上滑动退出手势在所有页面都生效，其他手势只在书籍页面生效
    // 对于非书籍页面，我们需要处理触摸事件以检测底部向上滑动，但其他手势不拦截

    // 调试日志：记录所有触摸/鼠标事件
    if (m_verboseLogs && isPointerEventType(type)) {
      QString eventTypeStr;
      QPointF pos;
      if (isMouseEventType(type)) {
        auto *me = static_cast<QMouseEvent *>(ev);
        eventTypeStr =
            (type == QEvent::MouseButtonPress     ? "MousePress"
             : type == QEvent::MouseButtonRelease ? "MouseRelease"
                                                  : "MouseMove");
        pos = me->position();
      } else {
        auto *te = static_cast<QTouchEvent *>(ev);
        eventTypeStr = (type == QEvent::TouchBegin    ? "TouchBegin"
                        : type == QEvent::TouchUpdate ? "TouchUpdate"
                                                      : "TouchEnd");
        if (!te->points().isEmpty()) {
          pos = te->points().first().position();
        }
      }
      qInfo() << "[GESTURE_DEBUG] eventFilter received" << eventTypeStr << "pos"
              << pos << "obj" << (obj ? obj->metaObject()->className() : "null")
              << "isWeReadKindleMode"
              << (m_browser ? m_browser->isWeReadKindleMode() : false)
              << "isBookPage" << isBookPage;
    }

    // 处理触摸/鼠标事件
    bool handled = false;
    switch (type) {
    case QEvent::MouseButtonPress: {
      auto *me = static_cast<QMouseEvent *>(ev);
      if (isBookPage) {
        if (isDuplicatePointerEvent(QEvent::MouseButtonPress, me->position(),
                                    nowMs)) {
          if (m_verboseLogs) {
            qInfo() << "[GESTURE_DEBUG] drop duplicate mouse press pos"
                    << me->position();
          }
          return true;
        }
        recordPointerEvent(QEvent::MouseButtonPress, me->position(), nowMs);
        handled = handleContactDown(me->position());
      } else {
        // 非书籍页面：完全不处理，让事件直接传递给 WebEngine
        handled = false; // 让事件继续传递
      }
      break;
    }
    case QEvent::MouseMove: {
      auto *me = static_cast<QMouseEvent *>(ev);
      if (isBookPage) {
        handled = handleContactMove(me->position(), isBookPage);
      } else {
        // 非书籍页面：完全不处理，让事件直接传递给 WebEngine
        handled = false; // 让事件继续传递
      }
      break;
    }
    case QEvent::MouseButtonRelease: {
      auto *me = static_cast<QMouseEvent *>(ev);
      if (isBookPage) {
        if (isDuplicatePointerEvent(QEvent::MouseButtonRelease, me->position(),
                                    nowMs)) {
          if (m_verboseLogs) {
            qInfo() << "[GESTURE_DEBUG] drop duplicate mouse release pos"
                    << me->position();
          }
          return true;
        }
        recordPointerEvent(QEvent::MouseButtonRelease, me->position(), nowMs);
        handled = handleContactUp(me->position(), isBookPage);
      } else {
        // 非书籍页面：完全不处理，让事件直接传递给 WebEngine
        handled = false; // 让事件继续传递
      }
      break;
    }
    case QEvent::TouchBegin: {
      auto *te = static_cast<QTouchEvent *>(ev);
      if (te->points().isEmpty()) {
        if (m_verboseLogs) {
          qInfo() << "[GESTURE_DECISION] touch begin with no points"
                  << "isBookPage" << isBookPage;
        }
        return isBookPage;
      }
      const QPointF pos = te->points().first().position();
      if (isBookPage && isDuplicatePointerEvent(QEvent::TouchBegin, pos, nowMs)) {
        if (m_verboseLogs) {
          qInfo() << "[GESTURE_DEBUG] drop duplicate touch begin pos" << pos;
        }
        return true;
      }
      if (isBookPage) {
        recordPointerEvent(QEvent::TouchBegin, pos, nowMs);
      }
      // 对于非书籍页面，也要初始化手势状态机（用于识别全局手势）
      handled = handleContactDown(pos);
      // 非书籍页面：即使初始化了状态机，也放行事件给
      // WebEngine（除非是菜单区域）
      if (!isBookPage) {
        handled = false; // 让事件继续传递给 WebEngine
      }
      break;
    }
    case QEvent::TouchUpdate: {
      auto *te = static_cast<QTouchEvent *>(ev);
      if (te->points().isEmpty()) {
        if (m_verboseLogs) {
          qInfo() << "[GESTURE_DECISION] touch update with no points"
                  << "isBookPage" << isBookPage;
        }
        return isBookPage;
      }
      // 对于非书籍页面，也要更新手势状态（用于识别全局手势）
      handled = handleContactMove(te->points().first().position(), isBookPage);
      // 非书籍页面：即使更新了状态，也放行事件给 WebEngine
      if (!isBookPage) {
        handled = false; // 让事件继续传递给 WebEngine
      }
      break;
    }
    case QEvent::TouchEnd: {
      auto *te = static_cast<QTouchEvent *>(ev);
      if (te->points().isEmpty()) {
        if (m_verboseLogs) {
          qInfo() << "[GESTURE_DECISION] touch end with no points"
                  << "isBookPage" << isBookPage;
        }
        return isBookPage;
      }
      const QPointF pos = te->points().first().position();
      if (isBookPage && isDuplicatePointerEvent(QEvent::TouchEnd, pos, nowMs)) {
        if (m_verboseLogs) {
          qInfo() << "[GESTURE_DEBUG] drop duplicate touch end pos" << pos;
        }
        return true;
      }
      if (isBookPage) {
        recordPointerEvent(QEvent::TouchEnd, pos, nowMs);
      }
      // 对于非书籍页面，也要调用 handleContactUp
      // 以识别全局手势（底部上滑退出、右侧左滑全刷） handleContactUp 会返回
      // true 如果识别到全局手势（需要拦截），否则返回 false（放行给 WebEngine）
      handled = handleContactUp(pos, isBookPage);
      // 对于非书籍页面，如果 handleContactUp 返回
      // true（全局手势被识别），则拦截事件；否则放行 对于书籍页面，根据
      // handleContactUp 的返回值决定是否拦截
      break;
    }
    default:
      break;
    }

    if (m_verboseLogs && isPointerEventType(type)) {
      QPointF pos;
      const bool hasPos = extractEventPos(ev, &pos);
      if (hasPos) {
        qInfo() << "[GESTURE_DECISION] return" << handled << "type"
                << eventTypeName(type) << "pos" << pos << "isBookPage"
                << isBookPage;
      } else {
        qInfo() << "[GESTURE_DECISION] return" << handled << "type"
                << eventTypeName(type) << "pos" << QStringLiteral("n/a")
                << "isBookPage" << isBookPage;
      }
    }

    // 对于非书籍页面，如果 handled 为
    // true（全局手势被识别），则拦截事件；否则放行给 WebEngine
    // 对于书籍页面，根据 handled 的值决定是否拦截
    // 注意：对于非书籍页面，handleContactUp 会返回 true
    // 如果识别到全局手势（底部上滑退出、右侧左滑全刷），否则返回 false
    return handled; // 根据手势处理结果决定是否拦截
  }

void GestureFilter::onHoldTimeout() {
    if (m_state == StateTap) {
      // 从 tap 状态转换到 hold 状态 (参考 KOReader holdState)
      m_state = StateHold;
      qInfo() << "[GESTURE] hold detected @" << m_startPos;
      // TODO: 可以在这里触发长按菜单等操作
    }
  }

bool GestureFilter::handleContactDown(const QPointF &pos) {
    // 如果触摸位置在菜单区域内（y=70-170），不处理，让事件传递给菜单
    if (pos.y() >= 70 && pos.y() <= 170) {
      qInfo() << "[GESTURE] contact down in menu area, skipping gesture "
                 "handling, pos="
              << pos;
      return false; // 不处理，让事件传递给菜单
    }

    m_state = StateTap;
    m_startPos = pos;
    m_currentPos = pos;
    m_panAccumulated = QPointF(0, 0);
    m_timer.restart();
    m_timer.start();
    m_holdTimer.start(); // 开始计时长按
    qInfo() << "[GESTURE] contact down @" << pos << "state -> StateTap";
    return true;
  }

bool GestureFilter::handleContactMove(const QPointF &pos, bool isBookPage ) {
    if (m_state == StateIdle)
      return true;

    m_currentPos = pos;
    const qreal dx = pos.x() - m_startPos.x();
    const qreal dy = pos.y() - m_startPos.y();
    const qreal absDx = qAbs(dx);
    const qreal absDy = qAbs(dy);

    // 在 StateTap 状态下，检测是否移动超过阈值 -> 转为 Pan
    if (m_state == StateTap) {
      if (absDx >= PAN_THRESHOLD || absDy >= PAN_THRESHOLD) {
        // 停止长按定时器，切换到 pan 状态
        m_holdTimer.stop();
        m_state = StatePan;
        m_panStartPos = m_startPos; // 记录 pan 起始点
        qInfo() << "[GESTURE] state -> StatePan, moved" << absDx << absDy;
      }
    }

    // 在 StatePan 状态下，实时处理滚动 (参考 KOReader handlePan)
    // 注意：只在书籍页面执行滚动，非书籍页面让 WebEngine 自己处理
    if (m_state == StatePan && isBookPage) {
      Direction dir = getDirection(dx, dy);
      // 垂直方向：实时滚动
      if (dir == DirNorth || dir == DirSouth) {
        // 计算增量滚动（相对于上次位置）
        const qreal deltaY = pos.y() - m_lastPanPos.y();
        if (qAbs(deltaY) > 5.0) { // 最小增量阈值
          m_browser->scrollByJs(static_cast<int>(deltaY));
          m_panAccumulated.ry() += deltaY;
          m_lastPanPos = pos;
          qInfo() << "[GESTURE] pan scroll deltaY" << deltaY << "total"
                  << m_panAccumulated.y();
        }
      }
      // 水平方向：暂不实时处理，等 swipe 判定
    } else if (m_state == StatePan && !isBookPage) {
      // 非书籍页面：只更新位置，不执行滚动
      m_lastPanPos = pos;
    }

    // 在 StateHold 状态下，检测是否开始 hold_pan
    if (m_state == StateHold) {
      if (absDx >= PAN_THRESHOLD || absDy >= PAN_THRESHOLD) {
        // hold_pan 模式：长按后拖动
        qInfo() << "[GESTURE] hold_pan detected";
        // TODO: 实现 hold_pan 操作（如选择文本）
      }
    }

    // 对于非书籍页面，返回 false 让事件传递给 WebEngine（用于滚动等操作）
    // 对于书籍页面，返回 true 拦截事件（因为我们自己处理滚动）
    if (!isBookPage) {
      return false; // 不拦截，让 WebEngine 处理滚动
    }

    return true; // 书籍页面：拦截事件，我们自己处理滚动
  }

bool GestureFilter::handleContactUp(const QPointF &pos, bool isBookPage ) {
    m_holdTimer.stop();

    // 如果触摸位置在菜单区域内（y=70-170），不处理，让事件传递给菜单
    if (pos.y() >= 70 && pos.y() <= 170) {
      qInfo() << "[GESTURE] contact up in menu area, skipping gesture "
                 "handling, pos="
              << pos;
      m_state = StateIdle; // 重置状态
      return false;        // 不处理，让事件传递给菜单
    }

    if (m_state == StateIdle)
      return true;

    m_currentPos = pos;
    const qreal dx = pos.x() - m_startPos.x();
    const qreal dy = pos.y() - m_startPos.y();
    const qreal absDx = qAbs(dx);
    const qreal absDy = qAbs(dy);
    const qint64 ms = m_timer.isValid() ? m_timer.elapsed() : 0;
    const qreal distance = qSqrt(dx * dx + dy * dy);
    Direction dir = getDirection(dx, dy);

    qInfo() << "[GESTURE] contact up @" << pos << "state" << m_state << "dx"
            << dx << "dy" << dy << "ms" << ms << "dir" << directionName(dir)
            << "isBookPage" << isBookPage;

    // ==================== 手势判定逻辑 ====================

    // 底部上滑退出：从屏幕底部边缘外（或非常接近底部边缘）上滑，在所有页面都生效（优先级最高）
    if (m_state == StatePan && dir == DirNorth) {
      // 从屏幕底部边缘外开始：起始位置在底部20像素内（y >= 1676，窗口高度1696）
      const qreal bottomEdgeThreshold =
          m_windowHeight - 20.0; // 底部20像素区域（接近屏幕外）
      const bool fromBottomEdge = m_startPos.y() >= bottomEdgeThreshold;
      if (fromBottomEdge && ms < SWIPE_INTERVAL_MS &&
          absDy >= SWIPE_MIN_DISTANCE) {
        qInfo()
            << "[GESTURE] bottom edge swipe up detected -> exit app (startY="
            << m_startPos.y() << "windowHeight=" << m_windowHeight
            << "dy=" << dy << "ms=" << ms << ")";
        if (m_browser) {
          // 先保存会话状态
          m_browser->saveSessionState();
          // 然后退出（标记为上滑退出）
          m_browser->exitToXochitl(QStringLiteral("swipe_up"));
        }
        m_state = StateIdle;
        return true; // 已处理，拦截事件
      }
    }

    // 右侧边缘外左滑全刷：从屏幕右侧边缘外（或非常接近右侧边缘）左滑至少200px，在所有页面都生效（用于手动清理残影）
    if (m_state == StatePan && dir == DirWest) {
      // 窗口宽度是954像素，右侧边缘外：起始位置在右侧20像素内（x >= 934）
      const qreal windowWidth = 954.0; // 固定窗口宽度
      const qreal rightEdgeThreshold =
          windowWidth - 20.0; // 右侧20像素区域（接近屏幕外）
      const bool fromRightEdge = m_startPos.x() >= rightEdgeThreshold;
      const qreal minSwipeDistance = 200.0; // 至少左滑200像素
      if (fromRightEdge && ms < SWIPE_INTERVAL_MS &&
          absDx >= minSwipeDistance) {
        qInfo() << "[GESTURE] right edge swipe left detected -> full refresh "
                   "(startX="
                << m_startPos.x() << "windowWidth=" << windowWidth
                << "dx=" << dx << "absDx=" << absDx << "ms=" << ms << ")";
        if (m_browser) {
          m_browser->triggerFullRefresh();
        }
        m_state = StateIdle;
        return true; // 已处理，拦截事件
      }
    }

    // 对于非书籍页面，不拦截其他手势，让事件正常传递给 WebEngine
    // WebEngine 会自动将触摸事件转换为鼠标事件
    if (!isBookPage) {
      m_state = StateIdle;
      return false; // 不拦截，让事件正常传递
    }

    // 书籍页面：处理所有手势
    if (m_state == StateTap) {
      // 在 tap 状态释放 -> 判定为 tap (参考 KOReader handleDoubleTap)
      if (absDx < TAP_BOUNCE_DISTANCE && absDy < TAP_BOUNCE_DISTANCE) {
        // Kindle 模式的微信书籍页：tap 直接触发下一页按钮
        const bool isKindleMode =
            m_browser ? m_browser->isWeReadKindleMode() : false;
        const bool isWeReadBook = m_browser ? m_browser->isWeReadBook() : false;
        const bool isKindleUA = m_browser ? m_browser->isKindleUA() : false;
        const bool isCenterTap = isCenterZone(pos);
        if (m_verboseLogs) {
          qInfo() << "[GESTURE_DEBUG] tap check: isWeReadKindleMode"
                  << isKindleMode << "isWeReadBook" << isWeReadBook
                  << "isKindleUA" << isKindleUA << "absDx" << absDx << "absDy"
                  << absDy << "TAP_BOUNCE_DISTANCE" << TAP_BOUNCE_DISTANCE
                  << "centerTap" << isCenterTap;
        }
        if (m_browser && isCenterTap) {
          qInfo() << "[GESTURE] tap in center zone -> inject mouse click";
          m_browser->injectMouse(Qt::LeftButton, QEvent::MouseButtonPress, pos);
          m_browser->injectMouse(Qt::LeftButton, QEvent::MouseButtonRelease,
                                 pos);
          return true;
        }
        if (m_browser && isKindleMode) {
          const qint64 tapTime = QDateTime::currentMSecsSinceEpoch();
          qInfo() << "[GESTURE] tap -> next (weRead kindle mode) timestamp"
                  << tapTime;
          m_browser->goNextPage(pos);
          return true;
        } else {
          qInfo() << "[GESTURE] tap detected @" << pos
                  << "-> inject mouse click";
          m_browser->injectMouse(Qt::LeftButton, QEvent::MouseButtonPress, pos);
          m_browser->injectMouse(Qt::LeftButton, QEvent::MouseButtonRelease,
                                 pos);
          return true;
        }
      } else {
        if (m_verboseLogs) {
          qInfo() << "[GESTURE_DEBUG] tap rejected: movement too large, absDx"
                  << absDx << "absDy" << absDy;
        }
        return false;
      }
    } else if (m_state == StatePan) {
      // 在 pan 状态释放 -> 判定是 swipe 还是 pan_release
      // 参考 KOReader: isSwipe() 检查时间 < SWIPE_INTERVAL_MS
      if (ms < SWIPE_INTERVAL_MS && distance >= SWIPE_MIN_DISTANCE) {
        // 快速滑动 -> Swipe (参考 KOReader handleSwipe)
        if (dir == DirWest) {
          // 左滑 -> 下一页
          qInfo() << "[GESTURE] swipe west -> next page";
          m_browser->goNextPage();
          return true;
        } else if (dir == DirEast) {
          // 右滑：区分边缘退出 vs 上一页
          const bool edgeSwipe = m_startPos.x() < 60.0; // 屏幕左缘外/近缘
          if (edgeSwipe && m_browser && m_browser->isWeReadBook()) {
            qInfo() << "[GESTURE] edge swipe east -> weread home";
            m_browser->goWeReadHome();
            return true;
          } else {
            qInfo() << "[GESTURE] swipe east -> prev page";
            m_browser->goPrevPage();
            return true;
          }
        } else if (dir == DirSouth) {
          // 顶部下滑 -> 呼出菜单（优先级高于滚动）
          const bool fromTop = m_startPos.y() < 180.0; // 顶部阈值
          if (fromTop && dy > 80.0 && ms < 1500) {
            qInfo() << "[GESTURE] top pull-down detected, show menu (startY="
                    << m_startPos.y() << "dy=" << dy << "ms=" << ms << ")";
            if (m_browser) {
              m_browser->showMenu();
              m_browser->scheduleBookCaptures();
            }
            m_state = StateIdle;
            return true; // 已处理，拦截事件
          }
          // 非顶部下滑：已经在 pan 中实时处理了滚动
          qInfo()
              << "[GESTURE] swipe south -> pan already handled, total scroll"
              << m_panAccumulated.y();
          return true; // 已处理，拦截事件
        } else if (dir == DirNorth) {
          // 上滑 -> 已经在 pan 中实时处理了滚动
          qInfo()
              << "[GESTURE] swipe north -> pan already handled, total scroll"
              << m_panAccumulated.y();
          return true; // 已处理，拦截事件
        }
      } else {
        // 慢速移动 -> pan_release (参考 KOReader handlePanRelease)
        // 检查是否是顶部下滑手势（即使是慢速移动也应该能呼出菜单）
        const bool fromTop = m_startPos.y() < 180.0; // 顶部阈值
        if (fromTop && dir == DirSouth && dy > 80.0 && ms < 1500) {
          qInfo() << "[GESTURE] slow top pull-down detected, show menu (startY="
                  << m_startPos.y() << "dy=" << dy << "ms=" << ms << ")";
          if (m_browser) {
            m_browser->showMenu();
            m_browser->scheduleBookCaptures();
          }
          m_state = StateIdle;
          return true; // 已处理，拦截事件
        }
        qInfo() << "[GESTURE] pan_release, total scroll"
                << m_panAccumulated.y();
        // 垂直 pan 已经实时处理，无需额外操作
        return true; // 已处理，拦截事件
      }
    } else if (m_state == StateHold) {
      // 长按释放 (参考 KOReader hold_release)
      qInfo() << "[GESTURE] hold_release @" << pos;
      // TODO: 可以在这里处理长按释放后的操作
    }

    // 重置状态
    m_state = StateIdle;
    m_lastPanPos = m_startPos; // 重置 pan 位置
    return true;
  }

GestureFilter::Direction GestureFilter::getDirection(qreal dx, qreal dy) const {
    if (dx == 0 && dy == 0)
      return DirNone;
    // 简化版：只判断主方向（不含对角线）
    if (qAbs(dx) > qAbs(dy)) {
      return dx < 0 ? DirWest : DirEast;
    } else {
      return dy < 0 ? DirNorth : DirSouth;
    }
  }

const char *GestureFilter::directionName(GestureFilter::Direction dir) {
    switch (dir) {
    case DirNone:
      return "none";
    case DirNorth:
      return "north";
    case DirSouth:
      return "south";
    case DirEast:
      return "east";
    case DirWest:
      return "west";
    }
    return "unknown";
  }

bool GestureFilter::isSamePosition(const QPointF &a, const QPointF &b, qreal tol) {
    const qreal dx = a.x() - b.x();
    const qreal dy = a.y() - b.y();
    return (dx * dx + dy * dy) <= (tol * tol);
  }

bool GestureFilter::isDuplicatePointerEvent(QEvent::Type type, const QPointF &pos, qint64 nowMs) const {
    if (m_lastPointerMs < 0)
      return false;
    if (m_lastPointerType != type)
      return false;
    if (nowMs - m_lastPointerMs > DUPLICATE_EVENT_MS)
      return false;
    return isSamePosition(pos, m_lastPointerPos, DUPLICATE_POS_TOL);
  }

void GestureFilter::recordPointerEvent(QEvent::Type type, const QPointF &pos, qint64 nowMs) {
    m_lastPointerType = type;
    m_lastPointerPos = pos;
    m_lastPointerMs = nowMs;
  }

void GestureFilter::noteTouchEvent(const QPointF &pos, qint64 nowMs) {
    m_lastTouchMs = nowMs;
    m_lastTouchPos = pos;
  }

bool GestureFilter::hasRecentTouch(const QPointF &pos, qint64 nowMs) const {
    if (m_lastTouchMs < 0)
      return false;
    if (nowMs - m_lastTouchMs > SYNTH_MOUSE_GUARD_MS)
      return false;
    return isSamePosition(pos, m_lastTouchPos, SYNTH_MOUSE_POS_TOL);
  }

bool GestureFilter::isCenterZone(const QPointF &pos) const {
    qreal width = m_browser ? static_cast<qreal>(m_browser->width()) : 954.0;
    if (width <= 0.0)
      width = 954.0;
    qreal height =
        m_windowHeight > 0 ? static_cast<qreal>(m_windowHeight) : 1696.0;
    const qreal rx = pos.x() / width;
    const qreal ry = pos.y() / height;
    return rx >= 0.4 && rx <= 0.6 && ry >= 0.4 && ry <= 0.6;
  }
