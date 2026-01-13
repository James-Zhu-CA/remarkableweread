#!/bin/bash
LOG="/home/root/xovi/rebuild_auto.log"
exec > >(tee -a "$LOG") 2>&1

echo "Starting rebuild_and_verify..."
systemctl stop xochitl
killall -9 xochitl 2>/dev/null
while pidof xochitl >/dev/null; do
    echo "Waiting for xochitl to die..."
    sleep 1
done
echo "xochitl stopped."

HASHTAB="/home/root/xovi/exthome/qt-resource-rebuilder/hashtab"
BACKUP="/home/root/xovi/exthome/qt-resource-rebuilder/hashtab.bak"

# Clean previous
rm -f "$HASHTAB"

# Start rebuild
(
    cd /home/root/xovi
    export QMLDIFF_HASHTAB_CREATE="$HASHTAB"
    export QML_DISABLE_DISK_CACHE=1
    export LD_PRELOAD=/home/root/xovi/xovi.so 
    /usr/bin/xochitl
) &
PID=$!

echo "Rebuilder PID: $PID"

# Wait for file
COUNT=0
while [ ! -f "$HASHTAB" ]; do
    sleep 2
    COUNT=$((COUNT+1))
    if [ $COUNT -gt 60 ]; then
        echo "Timeout waiting for hashtab"
        kill $PID
        exit 1
    fi
    # Check if process died
    if ! kill -0 $PID 2>/dev/null; then
        echo "Rebuilder process died prematurely"
        exit 1
    fi
done

echo "Hashtab created! Size: $(stat -c %s $HASHTAB)"
cp "$HASHTAB" "$BACKUP"
echo "Backed up to $BACKUP"

# Kill rebuilder
kill -15 $PID
sleep 5
kill -9 $PID 2>/dev/null

echo "Starting xochitl service..."
systemctl start xochitl

sleep 10
if [ -f "$HASHTAB" ]; then
    echo "Hashtab SURVIVED!"
    ls -l "$HASHTAB"
else
    echo "Hashtab DELETED!"
    echo "Restoring from backup..."
    cp "$BACKUP" "$HASHTAB"
fi

echo "Checking journal..."
journalctl -u xochitl --since "-60s" | grep -iE "Hashtab|AppLoad|qmldiff|xovi"
