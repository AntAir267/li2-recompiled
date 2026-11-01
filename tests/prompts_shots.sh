#!/bin/bash
# The button prompts in pictures: the town scene of tests/hashcheck.sh with prompts on, frames
# every 300 from 1950 (the front end, the opening's dialogue at 6450, on foot 8850, the skateboard
# 9150, the pause menu 9450): the controller with the game-style studs (pad), with LEGO studs
# (lego), and the keyboard alone (keys: LI2_KEYS instead of the controller).
#   tests/prompts_shots.sh   -> work/prompts/<variant>/frames
set -u
cd "$(dirname "$0")/.."
GAME=${GAME:-work/game}
[ -d "$GAME" ] || GAME=$HOME/.local/share/li2-native/game
PAD="1900:A,2100:A,8700:LY=-32767*300,9000:Y,9100:LY=-32767*200,9400:START"
KEYS="1900:RETURN,2100:RETURN,9400:ESCAPE"
run() {   # name options pad keys
    local d=work/prompts/$1
    rm -rf $d && mkdir -p $d/u
    [ -f work/testuser/gm.dls ] && cp work/testuser/gm.dls $d/u/
    printf '%s\n' $2 | tr '=' ' ' | sed 's/ / = /' > $d/u/options.txt
    LI2_PAD="$3" LI2_KEYS="$4" LI2_GPU_OFFSCREEN=1 LI2_DUMP_FRAMES=$d/frames LI2_DUMP_EVERY=300 LI2_DUMP_FROM=1950 \
        timeout 900 build/li2 --game "$GAME" --user $d/u --test --frames 9500 --logfile $d/run.log >/dev/null 2>&1
    echo "$1: exit $?, $(ls $d/frames 2>/dev/null | wc -l) frames"
    grep -m3 "prompts:\|FATAL\|fatal" $d/run.log
}
run pad "prompts=on" "$PAD" "" &
run lego "prompts=on prompt_style=lego" "$PAD" "" &
run keys "prompts=on" "" "$KEYS" &
wait
# the skateboard's legend: the test save (Pepper has the board), loaded from the menu, Y at 9650
d=work/prompts/board
rm -rf $d && mkdir -p $d/u
cp -r work/testuser/_data $d/u/
[ -f work/testuser/gm.dls ] && cp work/testuser/gm.dls $d/u/
echo "prompts = on" > $d/u/options.txt
LI2_PAD="1900:A,2050:DOWN,2110:A,2300:A,2420:DOWN,2480:A,7300:A,7500:A,7700:A,7900:LY=-32767*300,8250:A,8300:LX=32767*200,8700:LY=-32767*400,9650:Y,9800:LY=-32767*400" \
    LI2_GPU_OFFSCREEN=1 LI2_DUMP_FRAMES=$d/frames LI2_DUMP_EVERY=100 LI2_DUMP_FROM=9600 timeout 900 build/li2 --game "$GAME" --user $d/u \
    --test --frames 10300 --logfile $d/run.log >/dev/null 2>&1
echo "board: exit $?, $(ls $d/frames 2>/dev/null | wc -l) frames"
# the PROMPTS and CONTROLLER screens: Options > Controls, PROMPTS (the sixth row), BUTTON PROMPTS on
# (live), back; then CONTROLLER (the fifth)
d=work/prompts/menu
rm -rf $d && mkdir -p $d/u
[ -f work/testuser/gm.dls ] && cp work/testuser/gm.dls $d/u/
LI2_PAD="1900:DOWN,1960:A,2100:A,2300:DOWN,2340:DOWN,2380:DOWN,2420:DOWN,2460:DOWN,2520:A,2700:RIGHT,2800:DOWN,2840:RIGHT,3000:B,3200:UP,3260:A" \
    LI2_GPU_OFFSCREEN=1 LI2_DUMP_FRAMES=$d/frames LI2_DUMP_EVERY=100 LI2_DUMP_FROM=2600 timeout 600 build/li2 --game "$GAME" --user $d/u \
    --test --frames 3500 --logfile $d/run.log >/dev/null 2>&1
echo "menu: exit $?"
grep "menu:" $d/run.log
grep -v "^#" $d/u/options.txt | grep -i "prompt"
