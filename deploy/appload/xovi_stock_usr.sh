mount -o remount,rw /
rm /usr/lib/systemd/system/xochitl.service.d/xovi.conf 2>/dev/null
rm /etc/systemd/system/xochitl.service.d/xovi.conf 2>/dev/null
mount -o remount,ro /

systemctl daemon-reload
systemctl restart xochitl
