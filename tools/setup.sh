#!/bin/bash
# First-run setup for your own copy of LEGO Island 2:
#
#   tools/setup.sh <installed game folder> [<CD folder>]
#
# The installed folder holds "LEGO Island 2.exe" and _data (an install made with the CD's setup,
# under Wine or copied from Windows). The exe must be the English Version 2 as shipped; patched
# exes (no-CD, widescreen, the LI2 Mod Manager) are fine if the original is still beside them
# under another name (the mod manager keeps it as "LEGO Island 2.exe.orig"): it is found by its
# fingerprint. The CD folder (a copy of the disc, or the mounted disc) gives the movies; without
# it the game plays without them.
#
# What it does: links the files into ~/.local/share/li2-native/game (nothing is copied or
# changed), copies Windows' gm.dls into ~/.local/share/li2-native if it finds one, translates and
# builds (make), and adds LEGO Island 2 to the applications menu (make install).
set -eu
cd "$(dirname "$0")/.."
if [ $# -lt 1 ]; then
    sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
fi
src=$(realpath "$1")
cd_dir=${2:+$(realpath "$2")}
want=$(sed -n "s/.*'LEGO Island 2.exe', '\([0-9a-f]\{64\}\)'.*/\1/p" builds/en_v2.py)
user=$HOME/.local/share/li2-native
game=$user/game

[ -d "$src/_data" ] || [ -d "$src/_Data" ] || { echo "no _data folder in $src"; exit 1; }
data=$src/_data; [ -d "$data" ] || data=$src/_Data

exe=
for f in "$src"/*.exe "$src"/*.exe.* "$src"/*.orig; do
    [ -f "$f" ] || continue
    if [ "$(sha256sum "$f" | cut -d' ' -f1)" = "$want" ]; then exe=$f; break; fi
done
if [ -z "$exe" ]; then
    echo "No LEGO Island 2.exe as shipped (English Version 2, sha256 ${want:0:12}...) in $src."
    echo "Patched exes are not translated; reinstall from the CD, or put the original beside them."
    exit 1
fi
echo "game: $exe"

mkdir -p "$game"
ln -sfn "$exe" "$game/LEGO Island 2.exe"
ln -sfn "$data" "$game/_data"
if [ -n "$cd_dir" ]; then
    if [ -d "$cd_dir/LEGO Island 2/Movies" ] || [ -d "$cd_dir/Lego Island 2/Movies" ]; then
        ln -sfn "$cd_dir" "$game/CD"
        echo "movies: $cd_dir"
    else
        echo "no \"LEGO Island 2/Movies\" in $cd_dir: no movies"
    fi
fi

if [ ! -f "$user/gm.dls" ]; then
    for g in "$src/gm.dls" "$HOME"/.wine/drive_c/windows/system32/drivers/gm.dls \
             "$HOME"/.wine/drive_c/windows/syswow64/drivers/gm.dls; do
        if [ -f "$g" ] && [ "$(head -c 4 "$g")" = RIFF ]; then
            cp "$g" "$user/gm.dls"
            echo "gm.dls: $g"
            break
        fi
    done
    [ -f "$user/gm.dls" ] || echo "gm.dls: not found (some music instruments will be silent; see README)"
fi

make GAME="$game"
make GAME="$game" install
echo "Done: LEGO Island 2 is in the applications menu."
