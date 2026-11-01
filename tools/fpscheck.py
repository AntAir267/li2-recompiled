"""Frame-rate check, static: every function of the game read for arithmetic that only behaves at
the rate the game was made for. The game's clock (0x8a0850) is the frame time in 1/30 s; the
original had no frame cap, so code made at 30 fps meets clock 0.18 at 165 fps.

Values are followed through the x87 stack, the function's locals and the globals as powers of the
clock ("degree": 1 for k * clock, 0 for a plain value). The original is a debug build (every
statement goes through memory), so following them in address order holds within a function.
Kinds:
  compare  a value times the clock compared with a plain value, when that product is not also
           the step the function adds (then it is the usual "step, or snap when nearer": clamp).
           The Station Master's arrival test (radius * clock < distance) was one.
  clamp    the step-or-snap compare (right at any rate; listed for completeness)
  round    a per-frame amount (times the clock) made an integer: rounds to 0 at high rates
  scale    a field multiplied by k * clock and stored back (a decay wants k ^ clock)
  damp     a field multiplied by (1 - k * clock): first-order decay, off by more as k grows
  power    clock squared or divided by the clock (physics; usually right)
  step     a float field changed by a constant with no clock in the function: once a frame?
  count    an integer field changed by a constant: a frame counter?
Each finding: the instruction, its function, what was found, and whether builds/en_v2.py handles
it already (FRAME_STEPS, FRAME_COUNTS, TICKS, MEM_OVERRIDES, a hook on the function) or has
read it and found it right (FPSCHECK_REVIEWED: instruction or function -> why). With a
coverage file (runtime/ftrace.c's LI2_COVER: function, calls) the calls a frame are added, so code
run once a frame can be told from code not reached in the tests.

usage: tools/fpscheck.py <game folder> <build config> [cover.txt frames [more cover.txt...]] > report.txt
(calls a frame from the first coverage file and its frames; 'elsewhere': run in another one)"""
import bisect, struct, sys, os
sys.path.insert(0, os.path.dirname(__file__))
from pe import PE
from x86 import Decoder

CLOCK = 0x8a0850
FTOL = 0x8409c0


class T:
    """a value: degree in the clock, mixed (clock-scaled and plain added), the factors of a
    clock product (to match a compare with the step it guards), the memory it was loaded from
    and what it was multiplied by since (for x *= ...)"""
    __slots__ = ('deg', 'mixed', 'src', 'base', 'mulby', 'const', 'at')

    def __init__(self, deg=0, mixed=False, src=frozenset(), base=None, mulby=None, const=None, at=frozenset()):
        self.deg, self.mixed, self.src, self.base, self.mulby, self.const = deg, mixed, src, base, mulby, const
        self.at = at                # the clock reads it came from

    def clockish(self):
        return self.deg != 0 or self.mixed


PLAIN = T()


def memkey(op):
    _, size, seg, base, index, scale, disp = op
    if base is None and index is None:
        return ('g', disp)
    if base == 'ebp' and index is None:
        return ('l', disp)
    if base == 'esp':
        return None
    return ('f', disp if index is None else None)


class Func:
    def __init__(self, ck, addr, end):
        self.ck, self.addr, self.end = ck, addr, end
        self.locals = {}
        self.st = []
        self.steps = set()          # factor sets of clock products added as a step
        self.cmps = []              # (insn, scaled T, other T)
        self.regs = {}              # integer register -> (memory key it was loaded from, step)
        self.icmps = {}             # memory key -> immediates it is compared with
        self.uses_clock = False
        self.saved = {}             # forward jump target -> (locals, x87 stack) where it jumps from
        self.cut = False            # the instruction before ended its path (jmp, ret)

    # ---------------------------------------------------------------- x87 stack
    def push(self, t):
        self.st.insert(0, t)
        del self.st[8:]

    def pop(self):
        return self.st.pop(0) if self.st else PLAIN

    def sti(self, i):
        return self.st[i] if i < len(self.st) else PLAIN

    def set_sti(self, i, t):
        while len(self.st) <= i:
            self.st.append(PLAIN)
        self.st[i] = t

    def load(self, op, insn):
        k = memkey(op)
        if k is None:
            return PLAIN
        if k == ('g', CLOCK):
            self.uses_clock = True
            return T(1, src=frozenset(), base=None, at=frozenset([insn.addr]))
        if k[0] == 'l' and k in self.locals:
            t = self.locals[k]
            return T(t.deg, t.mixed, t.src, None, None, t.const, t.at)
        if k[0] == 'g' and k[1] in self.ck.gtaint:
            t = self.ck.gtaint[k[1]]
            self.uses_clock = True
            return T(t.deg, t.mixed, t.src, at=t.at)
        c = self.ck.rdata_float(k[1]) if k[0] == 'g' and op[1] == 4 else None
        return T(0, src=frozenset([k]), base=k if k[0] != 'l' else None, const=c)

    def store(self, op, t, insn):
        k = memkey(op)
        if k is None:
            return
        if k[0] == 'l':
            self.locals[k] = t
        elif t.clockish() and k[0] == 'g' and k[1] != CLOCK:
            self.ck.gwrites[k[1]] = t
        # x = x * f(clock), x = x + c (no clock)
        if t.base == k and k[0] != 'l':
            m = t.mulby
            if m == 'step':
                self.ck.steps_found.append((insn, self, k, t.at))
            elif m is not None and m.deg == 1 and not m.mixed:
                self.ck.find('scale', insn, self, 'field %s times %s' % (kname(k), self.ck.desc(m)), m)
            elif m is not None and m.mixed:
                self.ck.find('damp', insn, self, 'field %s times %s' % (kname(k), self.ck.desc(m)), m)

    def arith(self, insn, op_name, dest_i, src_t):
        d = self.sti(dest_i)
        r = combine(op_name, d, src_t)
        if op_name in ('add', 'sub', 'subr'):
            for a, b in ((d, src_t), (src_t, d)):
                if a.deg == 1 and not a.mixed and b.deg == 0:
                    self.steps.add(a.src)
        if r.deg >= 2 or r.deg < 0:
            if not (d.deg >= 2 or d.deg < 0 or src_t.deg >= 2 or src_t.deg < 0):
                self.ck.find('power', insn, self, 'clock^%d' % r.deg, r)
        # keep the loaded field through the operation for x = x op y
        if d.base is not None and not d.mixed and d.deg == 0:
            r.base = d.base
            if op_name == 'mul' and src_t.clockish():
                r.mulby = src_t
            elif op_name in ('add', 'sub', 'mul') and not src_t.clockish() and src_t.const is not None \
                    and not self.uses_clock_recent(insn):
                r.mulby = 'step'
                r.at = r.at | {insn.addr}   # (FRAME_STEPS names the arithmetic instruction)
            elif d.mulby is not None:
                r.mulby = d.mulby
        self.set_sti(dest_i, r)

    def uses_clock_recent(self, insn):
        return self.uses_clock

    # ---------------------------------------------------------------- one instruction
    def step(self, i):
        mn, ops = i.mn, i.ops
        # branches: code reached only by a jump starts from the state where the jump was taken
        # (not from the other arm's end: an if / else scaling the same local both ways)
        if self.cut and i.addr in self.saved:
            loc, st = self.saved[i.addr]
            self.locals, self.st = dict(loc), list(st)
        self.cut = mn in ('jmp', 'ret', 'retn')
        if mn.startswith('j') and ops and ops[0][0] == 'i' and ops[0][1] > i.addr:
            self.saved.setdefault(ops[0][1], (dict(self.locals), list(self.st)))
        if mn.startswith('f') and mn not in ('fnstsw', 'fstsw', 'fnstcw', 'fldcw', 'fwait', 'fnclex',
                                             'fnstenv', 'fldenv', 'fnsave', 'frstor', 'fninit', 'ffree'):
            self.fpu(i, mn, ops)
            return
        if mn == 'call':
            if ops and ops[0][0] == 'i' and ops[0][1] == FTOL:
                t = self.sti(0)
                if t.deg != 0 and not t.mixed:
                    self.ck.find('round', i, self, '(int)(%s)' % self.ck.desc(t), t)
                self.pop()
            else:
                self.st = []        # (debug code: calls are made with an empty stack, results in st0)
                self.push(PLAIN)
            self.regs = {}
            return
        self.ints(i, mn, ops)

    def fpu(self, i, mn, ops):
        regs = [o for o in ops if o[0] == 'r']
        mems = [o for o in ops if o[0] == 'm']

        def stn(o):                     # st(i) -> i (capstone names them 'st(0)'.. 'st(7)')
            d = ''.join(ch for ch in o[1] if ch.isdigit())
            return int(d) if o[1].startswith('st') and d else 0
        if mn in ('fld', 'fild'):
            if mems:
                t = self.load(mems[0], i) if mn == 'fld' else T(0)
                self.push(t)
            else:
                self.push(self.sti(stn(regs[0])))
        elif mn in ('fld1', 'fldz', 'fldpi', 'fldl2e', 'fldl2t', 'fldlg2', 'fldln2'):
            self.push(T(0, const=1.0 if mn == 'fld1' else 0.0 if mn == 'fldz' else None))
        elif mn in ('fst', 'fstp', 'fist', 'fistp', 'fisttp'):
            t = self.sti(0)
            if mems:
                if mn.startswith('fist') and t.deg != 0 and not t.mixed:
                    self.ck.find('round', i, self, '(int)(%s)' % self.ck.desc(t), t)
                self.store(mems[0], t, i)
            elif regs:
                self.set_sti(stn(regs[0]), t)
            if mn.endswith('p'):
                self.pop()
        elif mn == 'fxch':
            j = stn(regs[0]) if regs else 1
            a, b = self.sti(0), self.sti(j)
            self.set_sti(0, b)
            self.set_sti(j, a)
        elif mn[:4] in ('fadd', 'fsub', 'fmul', 'fdiv') or mn[:5] in ('fiadd', 'fisub', 'fimul', 'fidiv'):
            name = mn.lstrip('f').lstrip('i').rstrip('p')
            name = {'add': 'add', 'sub': 'sub', 'subr': 'subr', 'mul': 'mul', 'div': 'div', 'divr': 'divr'}[name]
            popit = mn.endswith('p')
            if mems:
                src = self.load(mems[0], i) if not mn.startswith('fi') else T(0)
                self.arith(i, name, 0, src)
            elif len(regs) == 2:
                self.arith(i, name, stn(regs[0]), self.sti(stn(regs[1])))
            elif len(regs) == 1:
                if popit:
                    self.arith(i, name, stn(regs[0]), self.sti(0))
                else:
                    self.arith(i, name, 0, self.sti(stn(regs[0])))
            else:
                self.arith(i, name, 1, self.sti(0))
            if popit:
                self.pop()
        elif mn.startswith(('fcom', 'fucom', 'ficom', 'ftst')):
            a = self.sti(0)
            if mems:
                b = self.load(mems[0], i) if not mn.startswith('fi') else T(0)
            elif regs:
                b = self.sti(stn(regs[-1]) if stn(regs[-1]) != 0 or len(regs) == 1 else 1)
            elif mn == 'ftst':
                b = T(0, const=0.0)
            else:
                b = self.sti(1)
            self.note_cmp(i, a, b)
            n = 2 if mn.endswith('pp') else 1 if mn.endswith('p') else 0
            for _ in range(n):
                self.pop()
        elif mn in ('fchs', 'fabs', 'frndint'):
            pass
        elif mn == 'fsqrt':
            t = self.sti(0)
            self.set_sti(0, T(t.deg // 2 if t.deg % 2 == 0 else 0, t.mixed or t.deg % 2 != 0, t.src))
        elif mn in ('fsin', 'fcos', 'fptan', 'f2xm1'):
            t = self.sti(0)
            self.set_sti(0, T(0, t.clockish()))
            if mn == 'fptan':
                self.push(T(0, const=1.0))
        elif mn in ('fpatan', 'fyl2x', 'fyl2xp1', 'fscale', 'fprem', 'fprem1'):
            a, b = self.sti(0), self.sti(1)
            if mn in ('fpatan', 'fyl2x', 'fyl2xp1'):
                self.pop()
                self.set_sti(0, T(0, a.clockish() or b.clockish()))
        elif mn == 'fsincos':
            t = self.sti(0)
            self.set_sti(0, T(0, t.clockish()))
            self.push(T(0, t.clockish()))
        # (others leave the stack as it is)

    def note_cmp(self, i, a, b):
        for s, o in ((a, b), (b, a)):
            if s.deg == 1 and not s.mixed and o.deg == 0 and not o.mixed:
                self.cmps.append((i, s, o))
                return
        if (a.deg != b.deg) and not a.mixed and not b.mixed and (a.deg or b.deg):
            self.ck.find('compare', i, self, 'clock^%d against clock^%d' % (a.deg, b.deg))

    # ---------------------------------------------------------------- integer counters
    def ints(self, i, mn, ops):
        if mn == 'lea' and len(ops) == 2 and ops[1][0] == 'm':
            k = memkey(ops[1])          # a local's address taken: a call may write it
            if k and k[0] == 'l':
                for d in range(k[1] - 64, k[1] + 64, 4):
                    self.locals.pop(('l', d & 0xffffffff), None)
        if mn == 'mov' and len(ops) == 2 and ops[0][0] == 'm' and ops[0][1] == 4 and ops[1][0] == 'i':
            k = memkey(ops[0])          # a float constant stored as its bits
            if k and k[0] == 'l':
                self.locals[k] = T(0, const=struct.unpack('<f', struct.pack('<I', ops[1][1]))[0])
        if mn == 'mov' and len(ops) == 2 and ops[0][0] == 'r' and ops[1][0] == 'm':
            k = memkey(ops[1])
            self.regs[ops[0][1]] = (k, None) if k and k[0] != 'l' else None
            if k == ('g', CLOCK):
                self.uses_clock = True
        elif mn in ('add', 'sub', 'inc', 'dec') and ops and ops[0][0] == 'r':
            r = self.regs.get(ops[0][1])
            if r and (mn in ('inc', 'dec') or (len(ops) == 2 and ops[1][0] == 'i')):
                self.regs[ops[0][1]] = (r[0], mn)
            else:
                self.regs.pop(ops[0][1], None)
        elif mn == 'mov' and len(ops) == 2 and ops[0][0] == 'm' and ops[1][0] == 'r':
            k = memkey(ops[0])
            r = self.regs.get(ops[1][1])
            if r and r[1] and r[0] == k and k[0] != 'l':
                self.ck.counts_found.append((i, self, k))
        elif mn in ('inc', 'dec', 'add', 'sub') and ops and ops[0][0] == 'm' and \
                (mn in ('inc', 'dec') or ops[1][0] == 'i'):
            k = memkey(ops[0])
            if k and k[0] != 'l':
                self.ck.counts_found.append((i, self, k))
        elif mn == 'cmp' and len(ops) == 2 and ops[1][0] == 'i':
            k = memkey(ops[0]) if ops[0][0] == 'm' else (self.regs.get(ops[0][1]) or (None,))[0]
            if k and k[0] != 'l':
                self.icmps.setdefault(k, set()).add(ops[1][1])
                if k[0] == 'g':
                    self.ck.gcmps.setdefault(k, set()).add(ops[1][1])
        elif ops and ops[0][0] == 'r':
            self.regs.pop(ops[0][1], None)

    def finish(self):
        for i, s, o in self.cmps:
            kind = 'clamp' if s.src in self.steps else 'compare'
            self.ck.find(kind, i, self, '%s against %s' % (self.ck.desc(s), self.ck.desc(o)), s)


def combine(op, d, s):
    at = d.at | s.at
    if op == 'mul' and 0.0 in (d.const, s.const):
        return T(0, const=0.0)
    if op in ('add', 'sub', 'subr') and s.const == 0.0:
        return T(d.deg, d.mixed, d.src, at=d.at)
    if op in ('add', 'sub', 'subr') and d.const == 0.0:
        return T(s.deg, s.mixed, s.src, at=s.at)
    if op == 'mul':
        return T(d.deg + s.deg, d.mixed or s.mixed, d.src | s.src, at=at)
    if op == 'div':
        return T(d.deg - s.deg, d.mixed or s.mixed, d.src | s.src, at=at)
    if op == 'divr':
        return T(s.deg - d.deg, d.mixed or s.mixed, d.src | s.src, at=at)
    if d.deg == s.deg:
        return T(d.deg, d.mixed or s.mixed, d.src | s.src, at=at)
    return T(0, True, d.src | s.src, at=at)


def kname(k):
    t, v = k
    if t == 'g':
        return 'ds:%x' % v
    if v is None:
        return '[indexed]'
    v = v - (1 << 32) if v >= 0x80000000 else v
    return ('local %+d' if t == 'l' else '+%#x') % v


class Check:
    def __init__(self, game, cfgpath):
        from recomp import load_config
        from image import Image, Module, Decoder as ImgDecoder
        from discover import Discovery
        cfg = load_config(cfgpath)
        name, fn, _ = cfg.MODULES[0]
        self.pe = Image([Module(name, os.path.join(game, fn))])
        self.dec = ImgDecoder(self.pe)
        iat = {va: n for dll, n, va in self.pe.imports}
        disc = Discovery(self.pe, self.dec, iat, cfg.NORETURN_IMPORTS, cfg.EXTRA_ENTRIES,
                         cfg.JUMP_TABLES, cfg.NOT_ENTRIES)
        self.funcs = disc.run(self.pe.seeds(), self.pe.data_code_pointers())
        self.gtaint = {}
        self.gwrites = {}
        self.findings = []
        self.steps_found = []
        self.counts_found = []
        self.gcmps = {}             # global -> immediates it is compared with anywhere

    def rdata_float(self, a):
        """a float constant of the read-only data (None for anything else)"""
        sec = self.pe.section_at(a)
        if sec and sec['name'] == '.rdata':
            return struct.unpack('<f', self.pe.bytes(a, 4))[0]
        return None

    def desc(self, t):
        parts = []
        for k in sorted(t.src, key=lambda k: (k[0], k[1] or 0)):
            c = self.rdata_float(k[1]) if k[0] == 'g' else None
            parts.append('%g' % c if c is not None else kname(k))
        return ' * '.join(parts + ['clock'] * max(t.deg, 0) + (['/clock'] * -t.deg if t.deg < 0 else [])) \
            + (' (mixed)' if t.mixed else '') or 'plain'

    def find(self, kind, insn, fn, text, t=None):
        self.findings.append((kind, insn.addr, fn.addr, text, t.at if t is not None else frozenset()))

    def run(self):
        for p in range(2):              # second pass: globals written with clock-scaled values
            self.findings, self.steps_found, self.counts_found, self.gcmps = [], [], [], {}
            self.gtaint = dict(self.gwrites) if p else {}
            self.gwrites = {}
            for a in sorted(self.funcs):
                f = Func(self, a, None)
                for x in sorted(self.funcs[a].insns):
                    f.step(self.funcs[a].insns[x])
                f.finish()
        for i, f, k, at in self.steps_found:
            self.findings.append(('step', i.addr, f.addr, 'field %s by a constant' % kname(k), frozenset(at)))
        for i, f, k in self.counts_found:
            if not f.uses_clock:
                c = f.icmps.get(k, set()) | (self.gcmps.get(k, set()) if k[0] == 'g' else set())
                c = sorted(v for v in c if 1 < v < 0x10000)
                self.findings.append(('count', i.addr, f.addr, 'integer %s by a constant%s' % (
                    kname(k), ', compared with ' + ' '.join(str(v) for v in c) if c else ''), frozenset()))


def handled_sets(build_py):
    g = {}
    exec(open(build_py).read(), g)
    insns = set()
    for name in ('FRAME_STEPS', 'FRAME_COUNTS'):
        insns |= set(g.get(name, {}))
    for k, v in g.get('MEM_OVERRIDES', {}).items():
        insns.add(v[0])
    funcs = set(g.get('HOOKS', {})) | set(g.get('TICKS', {}) if isinstance(g.get('TICKS'), dict) else g.get('TICKS', ()))
    return insns, funcs, g.get('FPSCHECK_REVIEWED', {})


def main():
    game, cfgpath = sys.argv[1], sys.argv[2]
    ck = Check(game, cfgpath)
    ck.run()
    insns, hooked, reviewed = handled_sets(cfgpath)
    cover, reached = {}, set()
    frames = 0
    if len(sys.argv) > 4:
        frames = float(sys.argv[4])
        for l in open(sys.argv[3]):
            a, n = l.split()
            cover[int(a, 16)] = int(n)
    for extra in sys.argv[5:]:          # other coverage files: run somewhere in the tests
        for l in open(extra):
            reached.add(int(l.split()[0], 16))
    order = ['compare', 'round', 'scale', 'damp', 'power', 'step', 'count', 'clamp']
    print('# tools/fpscheck.py: %d findings (kind insn func [calls/frame] [handled] what)' % len(ck.findings))
    for kind in order:
        rows = sorted(r for r in ck.findings if r[0] == kind)
        print('\n## %s: %d' % (kind, len(rows)))
        for _, ia, fa, text, at in rows:
            h = 'handled' if ia in insns or at & insns else 'reviewed' if ia in reviewed or fa in reviewed \
                else 'hooked-fn' if fa in hooked else ''
            cf = ''
            if frames:
                n = cover.get(fa, 0)
                cf = '%.2f/f' % (n / frames) if n else 'elsewhere' if fa in reached else 'not run'
            note = reviewed.get(ia, reviewed.get(fa, '')) if h == 'reviewed' else ''
            print('%-7s %06x %06x %-8s %-9s %s%s' % (kind, ia, fa, cf, h, text, '   -- ' + note if note else ''))


if __name__ == '__main__':
    main()
