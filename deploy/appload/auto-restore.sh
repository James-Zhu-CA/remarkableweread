#!/bin/bash
# Auto-restore script for xovi-tripletap
# Located at /home/root/xovi-tripletap/auto-restore.sh

LOGfile="/home/root/xovi-tripletap/auto-restore.log"
SERVICE_SRC="/home/root/xovi-tripletap/xovi-tripletap.service"
SERVICE_DEST="/etc/systemd/system/xovi-tripletap.service"

echo "=== tripletap restore start $(date) ===" >> "$LOGfile"

if [ ! -f "$SERVICE_SRC" ]; then
    echo "Error: Source service file not found at $SERVICE_SRC" >> "$LOGfile"
    exit 1
fi

# Copy service to /etc
# Note: /etc might be non-writable if root is RO, but this script runs as oneshot service.
# Usually startup scripts run before RO enforcement or rely on systemd capabilities?
# Actually, if root is RO, cp will fail unless we remount RW inside this script OR reliance on tmpfs overlay.
# The user's prompt implies "restoring to /etc" works. ReMarkable usually has /etc as overlay or writable during boot?
# OR the user relies on 'mount -o remount,rw /' inside this script?
# The user's prompt says "daemon-reload, enable, start".
# I'll add remount RO/RW just in case to be safe, but keep it minimal.
# Wait, user said "Avoid systemd interlock".
# I'll try without remount first, assuming /etc is writable (overlayfs) or handled.
# actually, better safe: remount rw if needed.

if touch /etc/test-rw 2>/dev/null; then
    rm /etc/test-rw
else
    mount -o remount,rw /
    MOUNTED_RW=1
fi

cp "$SERVICE_SRC" "$SERVICE_DEST"
chmod 644 "$SERVICE_DEST"

systemctl daemon-reload >> "$LOGfile" 2>&1
systemctl enable xovi-tripletap >> "$LOGfile" 2>&1
systemctl start xovi-tripletap --no-block >> "$LOGfile" 2>&1

if [ "$MOUNTED_RW" = "1" ]; then
    mount -o remount,ro /
fi

echo "=== tripletap restore end $(date) ===" >> "$LOGfile"
