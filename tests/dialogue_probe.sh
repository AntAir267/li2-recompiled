#!/bin/bash
# Which inputs end a line of dialogue early: the town's opening cutscene (New Game; its lines are
# the speech streams LECSIN01..), one input at frame 6400 (during the first line), the time the
# first line's stream ends and the second starts (--log sound) per input.
#   tests/dialogue_probe.sh [frame]   -> work/dlgp/<input>.log, a line per input
set -u
cd "$(dirname "$0")/.."
GAME=${GAME:-work/game}
[ -d "$GAME" ] || GAME=$HOME/.local/share/li2-native/game
at=${1:-6400}
probe() {   # name kind(key|pad) input
    local u=work/dlgp/u_$1
    rm -rf $u && mkdir -p $u
    [ -f work/testuser/gm.dls ] && cp work/testuser/gm.dls $u/
    local keys="" pad="1900:A,2100:A"
    [ "$2" = key ] && keys="$at:$3"
    [ "$2" = pad ] && pad="$pad,$at:$3"
    LI2_KEYS="$keys" LI2_PAD="$pad" LI2_GPU_OFFSCREEN=1 timeout 900 build/li2 --game "$GAME" --user $u --test \
        --frames 7400 --log sound --logfile work/dlgp/$1.log >/dev/null 2>&1
    local e1 s2
    e1=$(grep -m1 -A20 "LECSIN01" work/dlgp/$1.log | grep -m1 "ended" | awk '{print $1}')
    s2=$(grep -m1 "LECSIN02" work/dlgp/$1.log | awk '{print $1}')
    echo "$1: line 1 ended at ${e1:--} s, line 2 started at ${s2:--} s"
    rm -rf $u
}
mkdir -p work/dlgp
n=0
for p in "none none -" "SPACE key SPACE" "RETURN key RETURN" "ESCAPE key ESCAPE" "LCONTROL key LCONTROL" \
         "LSHIFT key LSHIFT" "TAB key TAB" "padA pad A" "padB pad B" "padX pad X" "padY pad Y"; do
    probe $p &
    n=$((n + 1)); [ $((n % 5)) = 0 ] && wait
done
wait
