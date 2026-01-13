#ifndef TOUCH_LOGGER_H
#define TOUCH_LOGGER_H

#include <QDebug>
#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QTouchEvent>

// 触摸/鼠标事件日志，排查无触摸反馈；不拦截事件
class TouchLogger : public QObject {
  Q_OBJECT
public:
  explicit TouchLogger(QObject *parent = nullptr) : QObject(parent) {}

protected:
  bool eventFilter(QObject *obj, QEvent *ev) override {
    switch (ev->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove: {
      auto *me = static_cast<QMouseEvent *>(ev);
      qInfo() << "[TOUCH_EVT] mouse"
              << (ev->type() == QEvent::MouseButtonPress     ? "press"
                  : ev->type() == QEvent::MouseButtonRelease ? "release"
                                                             : "move")
              << "pos" << me->position() << "global" << me->globalPosition()
              << "obj" << (obj ? obj->metaObject()->className() : "null")
              << "name" << (obj ? obj->objectName() : QString());
      break;
    }
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd: {
      auto *te = static_cast<QTouchEvent *>(ev);
      if (!te->points().isEmpty()) {
        const auto &pt = te->points().first();
        qInfo() << "[TOUCH_EVT] touch"
                << (ev->type() == QEvent::TouchBegin ? "begin"
                    : ev->type() == QEvent::TouchEnd ? "end"
                                                     : "update")
                << "pos" << pt.position() << "global" << pt.globalPosition()
                << "state" << pt.state()
                << "obj" << (obj ? obj->metaObject()->className() : "null")
                << "name" << (obj ? obj->objectName() : QString());
      } else {
        qInfo() << "[TOUCH_EVT] touch event with no points";
      }
      break;
    }
    default:
      break;
    }
    return QObject::eventFilter(obj, ev);
  }
};

#endif // TOUCH_LOGGER_H
