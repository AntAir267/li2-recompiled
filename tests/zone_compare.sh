#!/bin/bash
# The renderer's two paths in each zone: the town test save (work/testuser/_data/Save00.lgo) with
# its zone number set (save offset 8; flag 0x59), loaded from the menu and played by script (as
# work/zt/zones.sh), once with renderer d3d7 and once with LI2_RENDERER=native (the menus show
# neither); per zone: both exits, the game state (memory hashes with LI2_BENCH=1) compared, the
# native path's warnings (LI2_NATIVE_CHECK), frames every 1000 from 7500 for tests/imgdiff.py.
#   tests/zone_compare.sh <zone>...   -> work/zcmp/<zone>/{d3d7,native}/, a line per zone
# (Zones 13, 19, 27 and 34 crash with an edited save whichever the renderer: DESIGN.md.)
set -u
cd "$(dirname "$0")/.."
GAME=${GAME:-work/game}
[ -d "$GAME" ] || GAME=$HOME/.local/share/li2-native/game
PAD="1900:A,2050:DOWN,2110:A,2300:A,2420:DOWN,2480:A"
PAD="$PAD,7300:A,7500:A,7700:A,7900:LY=-32767*300,8250:A,8300:LX=32767*200,8550:X,8700:LY=-32767*400"
PAD="$PAD,9150:B,9300:LX=-32767*300,9650:Y,9800:LY=-32767*400,10300:A,10400:RX=20000*200,10700:A"
PAD="$PAD,10800:LY=32767*300,11150:X,11200:LY=-32767*250"
one() {   # zone renderer
    local z=$1 r=$2 d=work/zcmp/$1/$2
    rm -rf "$d" && mkdir -p "$d/user/_data"
    cp work/testuser/_data/* "$d/user/_data/"
    [ -n "${ZC_OPTS:-}" ] && printf '%s\n' "$ZC_OPTS" > "$d/user/options.txt"   # (e.g. ZC_OPTS="collision_fix = off")
    [ -f work/testuser/gm.dls ] && cp work/testuser/gm.dls "$d/user/"
    python3 -c "
import struct
p='$d/user/_data/Save00.lgo'; b=bytearray(open(p,'rb').read())
struct.pack_into('<I', b, 8, $z); open(p, 'wb').write(b)"
    local g=off
    [ "$r" = gpuxf ] && { r=d3d7; g=on; }
    LI2_GPU_TRANSFORM=$g LI2_RENDERER=$r LI2_BENCH=1 LI2_NATIVE_CHECK=1 LI2_PAD="$PAD" LI2_GPU_OFFSCREEN=1 LI2_DUMP_FRAMES="$d/frames" \
        LI2_DUMP_EVERY=1000 LI2_DUMP_FROM=7500 timeout 1800 build/li2 --game "$GAME" --user "$d/user" --test \
        --frames 11500 --hash "$d/hash.txt" --hash-every 60 --logfile "$d/run.log" >/dev/null 2>&1
    echo $? > "$d/exit"
}
A=${ZC_A:-d3d7}   # (ZC_A=gpuxf: GPU VERTICES on the Direct3D 7 path, the same picture as native)
zone() {
    local z=$1
    one $z $A & one $z native & wait
    local a=work/zcmp/$z/$A b=work/zcmp/$z/native nd
    nd=$(diff <(cut -d' ' -f1,3 $a/hash.txt) <(cut -d' ' -f1,3 $b/hash.txt) | grep -c '^<')
    echo "zone $z: exit $(cat $a/exit)/$(cat $b/exit), $(wc -l < $a/hash.txt)/$(wc -l < $b/hash.txt) hashes, state $nd differ,"\
         "$(grep -c 'native: vb#\|changing buffer\|not as expected\|without a box' $b/run.log) native warnings,"\
         "$(grep -o 'static meshes [0-9]*, mesh groups [0-9]*, rigid meshes [0-9]*' $b/run.log)"
}
n=0
for z in "$@"; do
    zone $z &
    n=$((n + 1))
    [ $((n % 3)) = 0 ] && wait
done
wait
