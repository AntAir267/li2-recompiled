#!/bin/bash
# The frame-rate check build: the generated code compiled with -DFTRACE (runtime/ftrace.c) into
# work/ftrace/li2, linked with build/'s runtime objects. Run `make` first.
set -e
cd "$(dirname "$0")/.."
mkdir -p work/ftrace/obj
CF="-std=gnu11 -g -fno-strict-aliasing -frounding-math -fno-omit-frame-pointer -Iruntime -Igen -Ithird_party/sdl3/include -O2 -w -DCPU_FAST_MEM -DFTRACE -finstrument-functions"
ls gen/*.c | xargs -P 8 -I{} sh -c 'o=work/ftrace/obj/$(basename {} .c).o; [ "$o" -nt {} ] && [ "$o" -nt runtime/cpu.h ] || gcc '"$CF"' -c {} -o $o'
gcc -o work/ftrace/li2 work/ftrace/obj/*.o $(ls build/obj/rt_*.o | grep -v _nomain) $(find build/obj/dm -name '*.o') -rdynamic third_party/sdl3/lib/libSDL3.a -pthread -lm -ldl -lz -lvulkan
echo built work/ftrace/li2
