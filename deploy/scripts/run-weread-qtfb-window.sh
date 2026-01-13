#!/bin/sh
# WeRead 窗口版（Appload qtfb 模式）
# 依赖：appload manifest 设置 qtfb:true，传入 QTFB_KEY

set -eu
APP_HOME=/home/root/weread
LOG_DIR="$APP_HOME/logs"
LOG_FILE="$LOG_DIR/weread-qtfb-window.log"
mkdir -p "$LOG_DIR"

exec >>"$LOG_FILE" 2>&1
set -x

echo "=== weread qtfb window $(date) ==="

# Appload 必须提供 QTFB_KEY
if [ -z "${QTFB_KEY:-}" ]; then
  echo "[ERROR] QTFB_KEY not set; ensure manifest qtfb:true"
  exit 1
fi

: "${WEREAD_KILL_OLD:=1}"

find_weread_pids() {
  for proc in /proc/[0-9]*; do
    [ -r "$proc/cmdline" ] || continue
    cmd="$(tr '\0' ' ' <"$proc/cmdline" 2>/dev/null || true)"
    case "$cmd" in
      *WereadEinkBrowser*) echo "${proc#/proc/}" ;;
    esac
  done
}

stop_existing_weread() {
  PIDS="$(find_weread_pids || true)"
  if [ -z "$PIDS" ]; then
    return 0
  fi

  echo "[INFO] stopping existing WereadEinkBrowser instances: $PIDS"
  for PID in $PIDS; do
    EXE="$(readlink "/proc/$PID/exe" 2>/dev/null || true)"
    echo "[INFO] pid=$PID exe=${EXE:-unknown}"
    kill "$PID" 2>/dev/null || true
  done

  for i in 1 2 3; do
    sleep 1
    PIDS="$(find_weread_pids || true)"
    [ -z "$PIDS" ] && break
  done

  if [ -n "$PIDS" ]; then
    echo "[WARN] force killing remaining instances: $PIDS"
    for PID in $PIDS; do
      kill -9 "$PID" 2>/dev/null || true
    done
  fi
}

if [ "$WEREAD_KILL_OLD" = "1" ]; then
  stop_existing_weread
else
  echo "[INFO] WEREAD_KILL_OLD=0; skip stopping old processes"
fi

export HOME=/home/root
export QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0:size=954x1696:depth=16
export QT_PLUGIN_PATH="$APP_HOME/qt6/plugins"
export QML2_IMPORT_PATH="$APP_HOME/qt6/qml"
export LD_LIBRARY_PATH="$APP_HOME/lib:$APP_HOME/qt6/lib:$APP_HOME/qt6/lib/nss:$APP_HOME/nss-libs:${LD_LIBRARY_PATH:-}"
export QTWEBENGINE_RESOURCES_PATH="$APP_HOME/qt6/resources"
export QTWEBENGINE_LOCALES_PATH="$APP_HOME/qt6/resources/locales"
export QTWEBENGINE_DISABLE_SANDBOX=1
export QTWEBENGINE_CHROMIUM_FLAGS="--disable-gpu --disable-gpu-compositing --no-sandbox --touch-events=enabled"
# Disable core script rewrite to avoid duplicate script execution during refetch.
export WEREAD_DISABLE_CORE_SCRIPT_REWRITE=1
# Diagnostics: netlog + remote debugging
: "${WEREAD_NETLOG:=/home/root/weread/logs/weread-netlog.json}"
export WEREAD_NETLOG
: "${QTWEBENGINE_REMOTE_DEBUGGING:=9222}"
export QTWEBENGINE_REMOTE_DEBUGGING
export QT_DEBUG_PLUGINS=1
# Enable Info logs for catalog debugging
export WEREAD_LOG_LEVEL=info
echo "[DIAG] WEREAD_NETLOG=${WEREAD_NETLOG} QTWEBENGINE_REMOTE_DEBUGGING=${QTWEBENGINE_REMOTE_DEBUGGING} QT_DEBUG_PLUGINS=${QT_DEBUG_PLUGINS} WEREAD_DISABLE_CORE_SCRIPT_REWRITE=${WEREAD_DISABLE_CORE_SCRIPT_REWRITE} WEREAD_LOG_LEVEL=${WEREAD_LOG_LEVEL}"

# qtfb shim
export LD_PRELOAD=/home/root/shims/qtfb-shim.so
export QTFB_SHIM_MODE=${QTFB_SHIM_MODE:-N_RGB565}
export QTFB_SHIM_INPUT_MODE=${QTFB_SHIM_INPUT_MODE:-NATIVE}
export QTFB_SHIM_MODEL=${QTFB_SHIM_MODEL:-0}

# 记录关键参数
echo "[INFO] QTFB_KEY=${QTFB_KEY}"

cd "$APP_HOME"
./WereadEinkBrowser "$@"
rc=$?
echo "[EXIT] WereadEinkBrowser exited with code=${rc}"
exit $rc
