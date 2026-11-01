#!/bin/bash
# GPU VERTICES (option gpu_transform) against the original CPU path: the scripted town scene of
# tests/hashcheck.sh run twice, off-screen, the same frames dumped from each (test runs: a virtual
# clock, so the frames show the same moment), then tests/imgdiff.py: each frame's largest and mean
# pixel difference, how many pixels differ, and side by side + difference images of the frame that
# differs most.
#   tests/gpuxf_compare.sh        -> work/gpuxf/{cpu,gpu}/frames, work/gpuxf/report.txt, *_side.png
set -u
cd "$(dirname "$0")/.."
GAME=${GAME:-work/game}
[ -d "$GAME" ] || GAME=$HOME/.local/share/li2-native/game
PAD="1900:A,2100:A,8700:LY=-32767*300,9000:Y,9100:LY=-32767*200,9400:START"
for mode in cpu gpu; do
    d=work/gpuxf/$mode
    rm -rf "$d" && mkdir -p "$d/user"
    [ -f work/testuser/gm.dls ] && cp work/testuser/gm.dls "$d/user/"
    [ $mode = gpu ] && echo "gpu_transform = on" > "$d/user/options.txt" || echo "gpu_transform = off" > "$d/user/options.txt"
    LI2_PAD="$PAD" LI2_GPU_OFFSCREEN=1 LI2_DUMP_FRAMES="$d/frames" LI2_DUMP_EVERY=300 LI2_DUMP_FROM=2400 \
        timeout 1200 build/li2 --game "$GAME" --user "$d/user" --test --frames 9600 --logfile "$d/run.log" >/dev/null 2>&1
    echo "$mode: exit $?, $(ls "$d/frames" 2>/dev/null | wc -l) frames"
done
python3 -I tests/imgdiff.py work/gpuxf/cpu/frames work/gpuxf/gpu/frames work/gpuxf | tee work/gpuxf/report.txt
