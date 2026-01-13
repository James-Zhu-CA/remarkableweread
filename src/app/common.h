#ifndef WEREAD_COMMON_H
#define WEREAD_COMMON_H

#include <QtGlobal>

// 共享日志级别枚举和全局变量
enum class LogLevel { Error = 0, Warning = 1, Info = 2 };

// 声明全局变量（定义在 main.cpp）
extern LogLevel g_logLevel;

// 日志级别检查函数
inline bool logLevelAtLeast(LogLevel level) {
  return static_cast<int>(g_logLevel) >= static_cast<int>(level);
}

#endif // WEREAD_COMMON_H
