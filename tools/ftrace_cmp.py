"""Compare two frame-rate check runs (runtime/ftrace.c): store sites whose values changed by more
in the higher-rate run, as many times more as it had frames, step once a frame.

usage: ftrace_cmp.py <low.txt> <low fps> <high.txt> <high fps> [funcs.txt]"""
import bisect, sys


def load(p):
    r = {}
    for l in open(p):
        a, n, ch = l.split()
        r[int(a, 16)] = (int(n), float(ch))
    return r


lo, flo, hi, fhi = load(sys.argv[1]), float(sys.argv[2]), load(sys.argv[3]), float(sys.argv[4])
k = fhi / flo
fs = []
if len(sys.argv) > 5:
    fs = sorted(int(l.split()[0], 16) for l in open(sys.argv[5]))
fn = lambda a: fs[bisect.bisect_right(fs, a) - 1] if fs else 0
rows = []
for a in sorted(set(lo) | set(hi)):
    nl, cl = lo.get(a, (0, 0.0))
    nh, ch = hi.get(a, (0, 0.0))
    if nl == 0 or nh == 0 or cl <= 0:
        continue
    rn, rc = nh / nl, ch / cl
    rows.append((rc, a, nl, nh, cl, ch, rn))
rows.sort(reverse=True)
print('%-8s %-8s %8s %8s %6s %12s %12s %6s' % ('site', 'func', 'n_lo', 'n_hi', 'n x', 'change_lo', 'change_hi', 'chg x'))
for rc, a, nl, nh, cl, ch, rn in rows:
    flag = ' <' if rc > k * 0.6 and rn > k * 0.6 else ''
    print('%08x %08x %8d %8d %6.2f %12.5g %12.5g %6.2f%s' % (a, fn(a), nl, nh, rn, cl, ch, rc, flag))
