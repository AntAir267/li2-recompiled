#!/bin/bash
# The CAMERA screen (Options > Controls > CAMERA): opened, CAMERA TILT moved down four steps
# (100 -> 60), back to Controls; frames every 100 from 2200 (work/menucam/frames), the log's
# "menu:" lines and the options.txt written. Then the town scene with camera_tilt = 50 and as
# shipped, a frame of each in the town (work/menucam/t50, t100).
set -u
cd "$(dirname "$0")/.."
GAME=${GAME:-work/game}
[ -d "$GAME" ] || GAME=$HOME/.local/share/li2-native/game
o=work/menucam
rm -rf $o && mkdir -p $o/u
[ -f work/testuser/gm.dls ] && cp work/testuser/gm.dls $o/u/
LI2_PAD="1900:DOWN,1960:A,2100:A,2300:DOWN,2340:DOWN,2380:DOWN,2440:A,2600:DOWN,2660:LEFT,2700:LEFT,2740:LEFT,2780:LEFT,2900:B" \
    LI2_GPU_OFFSCREEN=1 LI2_DUMP_FRAMES=$o/frames LI2_DUMP_EVERY=100 LI2_DUMP_FROM=2200 timeout 600 build/li2 --game "$GAME" \
    --user $o/u --test --frames 3100 --logfile $o/menu.log >/dev/null 2>&1
echo "menu run: exit $?"
grep "menu:" $o/menu.log
grep -i "camera" $o/u/options.txt
PAD="1900:A,2100:A,8700:LY=-32767*300,9000:Y,9100:LY=-32767*200,9400:START"
for t in 50 100; do
    mkdir -p $o/u$t; [ -f work/testuser/gm.dls ] && cp work/testuser/gm.dls $o/u$t/
    echo "camera_tilt = $t" > $o/u$t/options.txt
    LI2_PAD="$PAD" LI2_GPU_OFFSCREEN=1 LI2_DUMP_FRAMES=$o/t$t LI2_DUMP_EVERY=200 LI2_DUMP_FROM=8800 timeout 900 build/li2 \
        --game "$GAME" --user $o/u$t --test --frames 9300 --logfile $o/t$t.log >/dev/null 2>&1 &
done
wait
ls $o/t50 $o/t100 | head
