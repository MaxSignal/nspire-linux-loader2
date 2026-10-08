#!/bin/sh
# Scenarios for rootimg: run from loader/test
T=$(mktemp -d); trap 'rm -rf $T' EXIT
cfg() { printf 'image = %s/rootfs.img.tns\nsize = %s\nreserve = 2M\nmin = %s\n' "$T" "$1" "${2:-1M}" > $T/linux.cfg.tns; }
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
