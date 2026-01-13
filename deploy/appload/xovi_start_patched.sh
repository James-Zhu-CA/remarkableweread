mount -o remount,rw /
mkdir -p /etc/systemd/system/xochitl.service.d
umount /etc/systemd/system/xochitl.service.d 2>/dev/null || true

cat << END > /etc/systemd/system/xochitl.service.d/xovi.conf
[Service]
Environment="QML_DISABLE_DISK_CACHE=1"
Environment="QML_XHR_ALLOW_FILE_WRITE=1"
Environment="QML_XHR_ALLOW_FILE_READ=1"
Environment="LD_PRELOAD=/home/root/xovi/xovi.so"
END

mount -o remount,ro /
systemctl daemon-reload
systemctl restart xochitl
