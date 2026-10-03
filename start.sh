#!/bin/sh
# mkdb_server ko zinda rakho: mar jaye toh wajah log karo aur 1 sec baad dobara chalao
(
  while true; do
    ./mkdb_server /tmp/demo.db 7878 127.0.0.1
    echo "[start.sh] mkdb_server band hua (exit code $?), 1 sec me restart" >&2
    sleep 1
  done
) &
sleep 1
exec python playground/bridge.py 7878
