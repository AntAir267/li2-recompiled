#!/bin/bash
# Run-hash baselines: fixed scenes in test runs (--test: virtual clock, scripted controller), the
# memory and draw hashes of every 60th frame written down, and later runs compared with them.
# A change that should not alter the game (a refactoring, a translator change) leaves them equal.
#
#   tests/hashcheck.sh record     write work/baselines/<scene>.txt
#   tests/hashcheck.sh            compare with them: the first frame that differs, per scene
#
# Scenes: menu (start-up to the main menu), town (New Game, run, skateboard, the pause menu).
set -u
cd "$(dirname "$0")/.."
mode=${1:-compare}
GAME=${GAME:-work/game}
[ -d "$GAME" ] || GAME=$HOME/.local/share/li2-native/game   # (where tools/setup.sh links it)
mkdir -p work/baselines work/hashruns
user=work/hashruns/user
rm -rf "$user" && mkdir -p "$user"
[ -f work/testuser/gm.dls ] && cp work/testuser/gm.dls "$user/"

scene() {   # name frames pad-script
    local name=$1 frames=$2 pad=$3 out
    [ "$mode" = record ] && out=work/baselines/$name.txt || out=work/hashruns/$name.txt
    LI2_PAD="$pad" LI2_GPU_OFFSCREEN=1 timeout 1200 build/li2 --game "$GAME" --user "$user" --test \
        --frames "$frames" --hash "$out" --hash-every 60 --logfile "work/hashruns/$name.log" >/dev/null 2>&1
    local rc=$?
    if [ "$mode" = record ]; then
        echo "$name: recorded $(wc -l < "$out") hashes (exit $rc)"
    elif [ ! -s "$out" ]; then
        echo "$name: the run wrote no hashes (exit $rc; work/hashruns/$name.log)"
    elif [ ! -f "work/baselines/$name.txt" ]; then
        echo "$name: no baseline (tests/hashcheck.sh record)"
    else
        local d
        d=$(diff <(cut -d' ' -f1,3,4 "work/baselines/$name.txt") <(cut -d' ' -f1,3,4 "$out") | grep -m1 '^[<>]')
        if [ -z "$d" ]; then echo "$name: same ($(wc -l < "$out") hashes)"
        else echo "$name: differs from frame ${d#?? }"; fi
    fi
}

scene menu 1800 ""
scene town 9600 "1900:A,2100:A,8700:LY=-32767*300,9000:Y,9100:LY=-32767*200,9400:START"
