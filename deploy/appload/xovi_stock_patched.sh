mount -o remount,rw /
umount /etc/systemd/system/xochitl.service.d 2>/dev/null || true
rm /etc/systemd/system/xochitl.service.d/xovi.conf 2>/dev/null
mount -o remount,ro /

systemctl daemon-reload
systemctl restart xochitl
