#!/bin/sh
# Build the host test of rootimg.c. The sources are copied next to the
# shims, so that their #include "..." finds the shims and not the real
# headers.
set -e
cd "$(dirname "$0")"
mkdir -p build
cp shim/*.h build/
cp ../rootimg.c ../rootimg.h build/
gcc -O1 -Wall -Werror -Ibuild -o rootimg-test harness.c build/rootimg.c
