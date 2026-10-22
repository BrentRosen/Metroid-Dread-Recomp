#!/bin/bash
# Linux build: the newest glibc symbol version and the shared libraries each binary needs (what SteamOS must have)
for f in /opt/swr/bin/runtime_linux_x11 "/opt/swr/out/Metroid Dread_code.so"; do
  echo "== $f"
  objdump -T "$f" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -2
  readelf -d "$f" | grep NEEDED
done
