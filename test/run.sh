#!/bin/sh
# Scenarios for rootimg: run from loader/test
T=$(mktemp -d); trap 'rm -rf $T' EXIT
cfg() { printf 'image = %s/rootfs.img.tns\nsize = %s\nreserve = 2M\nmin = %s\n' "$T" "$1" "${2:-1M}" > $T/linux.cfg.tns
	[ -z "${3:-}" ] || echo "payload = $3" >> $T/linux.cfg.tns; }
# The payload a new image holds, as Linux reads it
payload() { python3 -I -c "
import struct, sys
d = open(sys.argv[1], 'rb').read()
assert d[16:24] == b'NSPLXPAY', 'no payload'
n = struct.unpack_from('<I', d, 24)[0]
out = b''.join(d[c * 4096 + 16:(c + 1) * 4096] for c in range(1, len(d) // 4096 - 1))[:n]
assert all(d[c * 4096:c * 4096 + 8] == b'NSPLXIMG' for c in range(len(d) // 4096 - 1))
sys.stdout.buffer.write(out)" "$1"; }
run() { ./rootimg-test $T $1 $T/linux.cfg.tns | grep -v "^$"; }
echo "== 1: size 8M, plenty of space";       cfg 8M;  run 100000000; python3 -I check.py $T/rootfs.img.tns
echo "== 2: size 8M again: left alone";       run 100000000; python3 -I check.py $T/rootfs.img.tns
echo "== 3: grow the fresh image to 12M";     cfg 12M; run 100000000; python3 -I check.py $T/rootfs.img.tns
echo "== 4: used image, 16M asked: kept";     python3 -I -c "
f=open('$T/rootfs.img.tns','r+b'); f.write(b'\0'*8192)"; cfg 16M; run 100000000; python3 -I check.py $T/rootfs.img.tns | sed 's/BAD/BAD (expected: formatted by Linux)/'
rm $T/rootfs.img.tns
echo "== 5: max with 20 MB of space";        cfg max; run 20000000; python3 -I check.py $T/rootfs.img.tns; ls $T
rm $T/rootfs.img.tns
echo "== 6: 64M asked, 20 MB of space";      cfg 64M; run 20000000; python3 -I check.py $T/rootfs.img.tns; ls $T
echo "== 7: a broken image is recreated";     head -c 5000 /dev/zero > $T/rootfs.img.tns; cfg 4M; run 100000000; python3 -I check.py $T/rootfs.img.tns
rm $T/rootfs.img.tns
echo "== 8: max without enough space";       cfg max; run 2500000; ls $T
echo "== 9: max, 6M needed, 7 MB of space";   cfg max 6M; run 7000000; ls $T
echo "== 10: 2M asked, 6M needed";             cfg 2M 6M; run 100000000; python3 -I check.py $T/rootfs.img.tns
rm $T/rootfs.img.tns
echo "== 11: fresh 4M image, max, 6M needed";  cfg 4M; run 100000000; cfg max 6M; run 100000000; python3 -I check.py $T/rootfs.img.tns
echo "== 12: used 4M image, max, 6M needed";   rm $T/rootfs.img.tns; cfg 4M; run 100000000; python3 -I -c "
f=open('$T/rootfs.img.tns','r+b'); f.write(b'\0'*8192)"; cfg max 6M; run 100000000
rm $T/rootfs.img.tns
echo "== 13: 6M asked and needed, 5 MB of space"; cfg 6M 6M; run 5000000; ls $T
head -c 300000 /dev/urandom > $T/payload.tns
echo "== 14: a new image with a payload";   cfg max 1M $T/payload.tns; run 20000000; python3 -I check.py $T/rootfs.img.tns; payload $T/rootfs.img.tns | cmp - $T/payload.tns && echo "payload OK"
rm $T/rootfs.img.tns
echo "== 15: an unused image without it";    cfg 4M; run 100000000; cfg 4M 1M $T/payload.tns; run 100000000; payload $T/rootfs.img.tns | cmp - $T/payload.tns && echo "payload OK"
echo "== 16: the same again: left alone";    run 100000000
echo "== 17: the payload deleted afterwards"; rm $T/payload.tns; run 100000000
rm $T/rootfs.img.tns
echo "== 18: no payload for a new image";    run 100000000; ls $T
