#!/bin/bash
# Two settings of the renderer against each other: the scripted town scene of tests/hashcheck.sh
# (start-up, the menus, New Game, the town) run twice, off-screen, with the options.txt lines A
# and B; the same frames dumped from each (test runs: a virtual clock, so the frames show the
# same moment) and compared by tests/imgdiff.py (each frame's largest and mean difference, how
# many pixels differ; side by side + difference images of the frame that differs most). Both runs
# also write the run hashes with LI2_BENCH=1 (render targets not mirrored into memory): the
# game's own state, whose memory column must be the same in both.
#   tests/render_compare.sh NAME "A lines" "B lines" [EVERY]   -> work/cmp/NAME/{a,b}, report.txt
# e.g. tests/render_compare.sh gpuxf "gpu_transform = off" "gpu_transform = on"
# A or B as "env VAR=value ..." sets environment variables instead (a setting the menus would
# show, and so change the game's memory, given without them: "env LI2_RENDERER=native").
set -u
cd "$(dirname "$0")/.."
name=$1 optA=$2 optB=$3 every=${4:-300}
GAME=${GAME:-work/game}
[ -d "$GAME" ] || GAME=$HOME/.local/share/li2-native/game
PAD="1900:A,2100:A,8700:LY=-32767*300,9000:Y,9100:LY=-32767*200,9400:START"
out=work/cmp/$name
rm -rf "$out" && mkdir -p "$out"
for side in a b; do
    d=$out/$side
    mkdir -p "$d/user"
    [ -f work/testuser/gm.dls ] && cp work/testuser/gm.dls "$d/user/"
    [ $side = a ] && o=$optA || o=$optB
    envs=()
    if [ "${o#env }" != "$o" ]; then read -ra envs <<< "${o#env }"; : > "$d/user/options.txt"; echo "# $o" > "$d/what.txt"
    else printf '%s\n' "$o" > "$d/user/options.txt"; echo "$o" > "$d/what.txt"; fi
    env "${envs[@]}" LI2_BENCH=1 LI2_NATIVE_CHECK=1 LI2_PAD="$PAD" LI2_GPU_OFFSCREEN=1 LI2_DUMP_FRAMES="$d/frames" LI2_DUMP_EVERY=$every \
        LI2_DUMP_FROM=60 timeout 1200 build/li2 --game "$GAME" --user "$d/user" --test --frames 9600 \
        --hash "$d/hash.txt" --hash-every 60 --logfile "$d/run.log" >/dev/null 2>&1
    echo "$side ($(head -1 "$d/what.txt")): exit $?, $(ls "$d/frames" 2>/dev/null | wc -l) frames, $(wc -l < "$d/hash.txt") hashes"
    grep -m3 "native: \|fatal\|ERROR" "$d/run.log"
done
nd=$(diff <(cut -d' ' -f1,3 "$out/a/hash.txt") <(cut -d' ' -f1,3 "$out/b/hash.txt") | grep -c '^<')
echo "game state (memory hashes): $nd of $(wc -l < "$out/a/hash.txt") differ$( [ $nd -gt 0 ] && echo ", first at frame $(diff <(cut -d' ' -f1,3 "$out/a/hash.txt") <(cut -d' ' -f1,3 "$out/b/hash.txt") | grep -m1 '^<' | cut -d' ' -f2)")"
if python3 -c "import numpy, PIL" 2>/dev/null; then
    python3 -I tests/imgdiff.py "$out/a/frames" "$out/b/frames" "$out" | tee "$out/report.txt"
else
    echo "(no numpy/PIL here: compare the frames elsewhere: tests/imgdiff.py $out/a/frames $out/b/frames $out)"
fi
