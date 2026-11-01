"""Simulate the M30 (Actions) shuffle as decompiled from mengine.ap bank 0x7b1a8000,
fed by an ideal RNG, to measure bias that comes from the algorithm itself."""
import random, sys

class Shuffle:
    def __init__(self, n, rng):
        self.n, self.rng = n, rng
        self.two_level = n > 64
        if self.two_level:
            self.cols = set(range(1, 65)); self.ncols = 64
            self.rows_full = (n >> 6) & 0xff          # stored in a byte: wraps above 16383
            self.last_cols = (n & 0x3f) + 1
        else:
            self.cols = set(range(1, n + 1)); self.ncols = n
        self.cur_col = 0

    def pick(self, pool, count, extra=0):
        """FUN_7b1a81d0 / 8258: 3 random tries, then linear scan from one end."""
        if count == 0:
            return 1
        span = count + extra
        last = 0
        for _ in range(3):
            last = self.rng.getrandbits(32) % span + 1
            if last in pool:
                pool.discard(last); return last
        rng = range(count, 0, -1) if last & 1 == 0 else range(1, count + 1)
        for i in rng:                                  # FUN_7b1a8370 (note: scans only 'count')
            if i in pool:
                pool.discard(i); return i
        return 0

    def init_rows(self):                               # FUN_7b1a8000
        self.nrows = self.rows_full
        extra = 1 if self.cur_col < self.last_cols else 0
        self.rows = set(range(1, self.nrows + extra + 1)); self.extra = extra

    def next_index(self):                              # FUN_7b1a84b0
        if not self.two_level:
            return self.pick(self.cols, self.ncols)
        if self.cur_col == 0:
            self.cur_col = self.pick(self.cols, self.ncols)
            if self.cur_col == 0: return 0
            self.init_rows()
        row = self.pick(self.rows, self.nrows, self.extra)
        if row == 0:
            self.cur_col = self.pick(self.cols, self.ncols)
            if self.cur_col == 0: return 0
            self.init_rows()
            row = self.pick(self.rows, self.nrows, self.extra)
            if row == 0: return 0
        return self.cur_col + (row - 1) * 64

def session(n, plays, rng):
    s, out = Shuffle(n, rng), []
    while len(out) < plays:
        i = s.next_index()
        if i == 0:
            s = Shuffle(n, rng); continue              # engine re-inits when exhausted
        if i <= n: out.append(i)
    return out

rng = random.Random(1)
print('%7s  %-26s %-24s %s' % ('tracks', 'reachable by shuffle', 'first 10% of list', 'consecutive picks'))
for n in (2000, 8000, 16000, 16384, 20000, 30000, 45000):
    picks = [i for _ in range(200) for i in session(n, 100, rng)]
    reach = max(session(n, n, rng))
    first10 = sum(i <= n * 0.1 for i in picks) / len(picks)
    seq = session(n, 6, rng)
    print('%7d  up to #%-6d (%5.1f%%)        %5.1f%% of picks           %s' % (n, reach, 100 * reach / n, 100 * first10, seq))
