#!/bin/sh
# Same-basename sources: the one named in SRCS is the one that gets built,
# and listing both is a build error.
cd "$(dirname "$0")" || exit 1
PREFIX=${PREFIX:-mipsel-none-elf}
status=0

make clean > /dev/null 2>&1
if ! make all; then
    echo "FAIL: build with one source of each pair"
    exit 1
fi
syms=$($PREFIX-nm libsamebasename.a)
for s in pick_from_c other_from_cpp; do
    echo "$syms" | grep -q " T $s\$" || { echo "FAIL: $s missing from the archive"; status=1; }
done
for s in pick_from_s other_from_c; do
    echo "$syms" | grep -q " T $s\$" && { echo "FAIL: $s built from a source SRCS does not name"; status=1; }
done

if make -f Makefile.both all > both.log 2>&1; then
    echo "FAIL: pick.c and pick.s together built instead of erroring"
    status=1
elif ! grep -q 'same object' both.log; then
    echo "FAIL: pick.c and pick.s together failed without the basename error:"
    cat both.log
    status=1
fi

make clean > /dev/null 2>&1
rm -f both.log
[ $status -eq 0 ] && echo "same-basename: OK"
exit $status
