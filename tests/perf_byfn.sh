#!/bin/bash
# A profile (tests/perf_town.sh) totalled by function: build/li2's samples named with addr2line.
#   tests/perf_byfn.sh work/perf/<name>.prof [N]
p=$1; n=${2:-25}
total=$(awk '{s+=$1} END{print s}' "$p")
grep " build/li2 " "$p" > /tmp/perf_s.$$
awk '{print "0x"$3}' /tmp/perf_s.$$ | addr2line -f -e build/li2 | awk 'NR%2==1' > /tmp/perf_f.$$
{ paste -d' ' <(awk '{print $1}' /tmp/perf_s.$$) /tmp/perf_f.$$; grep -v " build/li2 " "$p" | awk '{print $1, $2}'; } |
  awk -v t="$total" '{s[$2]+=$1} END{for(k in s) printf "%6.1f%%  %5d  %s\n", 100*s[k]/t, s[k], k}' | sort -rn | head -"$n"
echo "$total samples"
rm -f /tmp/perf_s.$$ /tmp/perf_f.$$
