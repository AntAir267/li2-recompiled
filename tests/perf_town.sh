#!/bin/bash
# Performance baseline: the scripted town scene of tests/hashcheck.sh (New Game, running, the
# skateboard, the pause menu), off-screen in a test run (virtual clock: as fast as it goes), measured
# from frame 8700 (in the town): the game thread's CPU ms a frame (LI2_CPU_FROM), a sampling profile
# (LI2_PROFILE, report by tools/profile.py) and the whole run's wall and CPU time.
# LI2_BENCH=1: GPU render targets are not mirrored into program memory (as in real play; test runs
# mirror them for the memory hashes).
#   tests/perf_town.sh <name>     -> work/perf/<name>.{log,prof,report,time}
set -u
cd "$(dirname "$0")/.."
name=${1:-run}
GAME=${GAME:-$HOME/.local/share/li2-native/game}
mkdir -p work/perf
user=work/perf/user; rm -rf "$user"; mkdir -p "$user"
[ -f work/testuser/gm.dls ] && cp work/testuser/gm.dls "$user/"
[ -n "${OPTIONS:-}" ] && printf '%s\n' "$OPTIONS" > "$user/options.txt"   # (e.g. OPTIONS="gpu_transform = on")
PAD="1900:A,2100:A,8700:LY=-32767*300,9000:Y,9100:LY=-32767*200,9400:START"
/usr/bin/time -f "wall %e s, user %U s, sys %S s, max rss %M KB" -o work/perf/$name.time \
  env LI2_BENCH=1 LI2_PAD="$PAD" LI2_GPU_OFFSCREEN=1 LI2_CPU_FROM=8700 LI2_PROFILE=work/perf/$name.prof LI2_PROFILE_FROM=8700 \
  build/li2 --game "$GAME" --user "$user" --test --frames 9600 --logfile work/perf/$name.log >/dev/null 2>&1
echo "exit $?"
.venv/bin/python tools/profile.py work/perf/$name.prof --top 40 > work/perf/$name.report 2>&1
cat work/perf/$name.time
grep "game thread CPU" work/perf/$name.log | tail -2
head -45 work/perf/$name.report
