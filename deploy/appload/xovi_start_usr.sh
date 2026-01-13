mount -o remount,rw /

# Use /usr/lib location which is confirmed to exist
CONF_PATH="/usr/lib/systemd/system/xochitl.service.d/xovi.conf"

# Remove any tmpfs mount if it exists (legacy)
umount /etc/systemd/system/xochitl.service.d 2>/dev/null || true

cat << END > "$CONF_PATH"
[Service]
Environment="QML_DISABLE_DISK_CACHE=1"
Environment="QML_XHR_ALLOW_FILE_WRITE=1"
Environment="QML_XHR_ALLOW_FILE_READ=1"
Environment="LD_PRELOAD=/home/root/xovi/xovi.so"
END

mount -o remount,ro /
systemctl daemon-reload
systemctl restart xochitl
