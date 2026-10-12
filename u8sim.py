"""u8sim - minimal nX-U8/100 simulator. Decodes raw machine code (does NOT share
tables with the assembler) so assembler + compiler + runtime get cross-checked.

Large model: PUSH LR / POP PC use 4 stack bytes (LR16 + LCSR + pad), per manual 1.6.
Flat 64KB, segment 0 only. LCD VRAM 0xF800 + y*32 + (x>>3) with 2 bitplanes
selected via 0xF037 (0 = low plane, 4 = high plane).
"""


class Halt(Exception):
    pass


class U8:
    def __init__(self, rom, keys=None, target=None, start=None, stop=()):
        from targets import get_target, DEFAULT_TARGET
        self.tg = get_target(target or DEFAULT_TARGET)
        self.core = self.tg.core          # nX-U8/100 (cwii) or nX-U16/100 (cw): same ISA, see u8asm.CORES
        self.mem = bytearray(max(65536, len(rom)))      # segment s lives at s<<16 (ROM only; RAM/SFR are segment 0)
        self.mem[:len(rom)] = rom
        self.r = [0] * 16
        self.csr = self.lcsr = 0
        self.dsr_next = self.dsr_cur = None
        self.stop = set(stop)
        self.pc = int.from_bytes(self.mem[2:4], 'little')
        self.sp = int.from_bytes(self.mem[0:2], 'little')
        if start:
            self.csr, self.pc = start
            self.sp = 0xF000
        self.lr = 0
        self.ea = 0
        self.psw = 0
        self.C = self.Z = self.S = self.OV = 0
        self.planes = [bytearray(0x800), bytearray(0x800)]
        self.steps = 0
        self.keys = keys or []      # list of (KO strobe mask -> KI byte) per read
        self.key_i = 0
        self.ko = 0

    # ---- memory ----
    def fetch16(self, a):
        b = (self.csr << 16) | (a & 0xFFFF)
        return self.mem[b] | (self.mem[b + 1] << 8)

    def rd(self, a):
        a &= 0xFFFF
        if self.dsr_cur:
            return self.mem[(self.dsr_cur << 16) | a]
        if 0xF800 <= a:
            return self.planes[1 if (self.tg.planes == 2 and self.mem[0xF037] & 4) else 0][a - 0xF800]
        if a == 0xF040:
            return self._key_in()
        return self.mem[a]

    def wr(self, a, v):
        a &= 0xFFFF
        v &= 0xFF
        if 0xF800 <= a:
            self.planes[1 if (self.tg.planes == 2 and self.mem[0xF037] & 4) else 0][a - 0xF800] = v
        else:
            if a < self.tg.ram_lo:
                raise Halt('write to ROM at %04X (pc=%04X)' % (a, self.pc))
            if a == 0xF037 and self.tg.planes == 1:
                raise Halt('write to 0F037H (plane select) on a 1-plane target (pc=%04X)' % self.pc)
            self.mem[a] = v
            if a == 0xF046:
                self.ko = v

    def _key_in(self):
        # emulate matrix: self.keys is a set of (ko_mask, ki_mask) pressed
        val = 0xFF
        for ko, ki in self.keys:
            if self.ko & ko:
                val &= ~ki & 0xFF
        return val

    def rd16(self, a):
        return self.rd(a) | (self.rd(a + 1) << 8)

    def wr16(self, a, v):
        self.wr(a, v)
        self.wr(a + 1, v >> 8)

    def er(self, n):
        return self.r[n] | (self.r[n + 1] << 8)

    def set_er(self, n, v):
        self.r[n] = v & 0xFF
        self.r[n + 1] = (v >> 8) & 0xFF

    # ---- flags ----
    def _f8(self, res, a, b, sub, carry_in=0, chain=False):
        r = res & 0xFF
        self.C = 1 if (res > 0xFF or res < 0) else 0
        z = 1 if r == 0 else 0
        self.Z = (self.Z & z) if chain else z
        self.S = r >> 7
        if sub:
            self.OV = 1 if ((a ^ b) & (a ^ r) & 0x80) else 0
        else:
            self.OV = 1 if (~(a ^ b) & (a ^ r) & 0x80) else 0
        return r

    def _f16(self, res, a, b, sub):
        r = res & 0xFFFF
        self.C = 1 if (res > 0xFFFF or res < 0) else 0
        self.Z = 1 if r == 0 else 0
        self.S = r >> 15
        if sub:
            self.OV = 1 if ((a ^ b) & (a ^ r) & 0x8000) else 0
        else:
            self.OV = 1 if (~(a ^ b) & (a ^ r) & 0x8000) else 0
        return r

    def _zs(self, r):
        self.Z = 1 if (r & 0xFF) == 0 else 0
        self.S = (r >> 7) & 1

    def cond(self, c):
        C, Z, S, OV = self.C, self.Z, self.S, self.OV
        return [not C, bool(C), (not C) and (not Z), bool(Z or C),
                not (OV ^ S), bool(OV ^ S), not ((OV ^ S) or Z), bool((OV ^ S) or Z),
                not Z, bool(Z), not OV, bool(OV), not S, bool(S), True][c]

    def push_bytes(self, bs):
        self.sp -= len(bs)
        for i, b in enumerate(bs):
            self.wr(self.sp + i, b)

    def pop_bytes(self, n):
        out = [self.rd(self.sp + i) for i in range(n)]
        self.sp += n
        return out

    # ---- execute one instruction ----
    def step(self):
        pc = self.pc
        if (self.csr, pc) in self.stop:
            raise Halt('stop at %X:%04X' % (self.csr, pc))
        self.dsr_cur, self.dsr_next = self.dsr_next, None
        w = self.fetch16(pc)
        self.steps += 1
        hi = w >> 12
        n = (w >> 8) & 0xF
        m = (w >> 4) & 0xF
        lo = w & 0xF
        i8 = w & 0xFF
        nxt = pc + 2

        if (w & 0xFF00) == 0xE300:          # DSR <- #imm8 : data segment of the NEXT instruction only
            self.dsr_next = i8
            self.pc = nxt
            return

        def sx8(v):
            return v - 256 if v & 0x80 else v

        # imm8 ALU
        if hi in (0, 1, 2, 3, 4, 5, 6, 7):
            a = self.r[n]
            if hi == 0:
                self.r[n] = i8; self._zs(i8)
            elif hi == 1:
                self.r[n] = self._f8(a + i8, a, i8, False)
            elif hi == 2:
                self.r[n] = a & i8; self._zs(self.r[n])
            elif hi == 3:
                self.r[n] = a | i8; self._zs(self.r[n])
            elif hi == 4:
                self.r[n] = a ^ i8; self._zs(self.r[n])
            elif hi == 5:
                self._f8(a - i8 - self.C, a, i8, True, chain=True)
            elif hi == 6:
                self.r[n] = self._f8(a + i8 + self.C, a, i8, False, chain=True)
            elif hi == 7:
                self._f8(a - i8, a, i8, True)
            self.pc = nxt
            return
        if hi == 8:
            if lo <= 9 and not (n & 0) :
                a, b = self.r[n], self.r[m]
                if lo == 0: self.r[n] = b; self._zs(b)
                elif lo == 1: self.r[n] = self._f8(a + b, a, b, False)
                elif lo == 2: self.r[n] = a & b; self._zs(self.r[n])
                elif lo == 3: self.r[n] = a | b; self._zs(self.r[n])
                elif lo == 4: self.r[n] = a ^ b; self._zs(self.r[n])
                elif lo == 5: self._f8(a - b - self.C, a, b, True, chain=True)
                elif lo == 6: self.r[n] = self._f8(a + b + self.C, a, b, False, chain=True)
                elif lo == 7: self._f8(a - b, a, b, True)
                elif lo == 8: self.r[n] = self._f8(a - b, a, b, True)
                elif lo == 9: self.r[n] = self._f8(a - b - self.C, a, b, True, chain=True)
                self.pc = nxt
                return
            if lo in (0xA, 0xB, 0xC, 0xD, 0xE):
                self._shift(lo, n, self.r[m])
                self.pc = nxt
                return
            if lo == 0xF:
                if m == 5 and (n & 0) == 0:       # NEG Rn: 1000_nnnn_0101_1111
                    a = self.r[n]
                    self.r[n] = self._f8(0 - a, 0, a, True)
                    self.pc = nxt
                    return
                if (n & 1) and not (m & 1):       # EXTBW ERn: 1000_nnn1_nnn0_1111
                    e = m
                    v = self.r[e]
                    self.r[e + 1] = 0xFF if v & 0x80 else 0
                    self.Z = 1 if self.er(e) == 0 else 0
                    self.S = self.r[e + 1] >> 7
                    self.pc = nxt
                    return
        if hi == 9:
            if lo in (0xA, 0xB, 0xC, 0xD, 0xE) and not (m & 8):
                self._shift(lo, n, m & 7)
                self.pc = nxt
                return
            if self._loadstore(w, nxt):
                return
        if hi == 0xA:
            if self._ctrl_a(w, nxt):
                return
            if self._loadstore(w, nxt):
                return
        if hi == 0xC:
            c = n
            r = sx8(i8)
            if c <= 0xE:
                self.pc = (nxt + 2 * r) & 0xFFFF if self.cond(c) else nxt
                return
        if hi == 0xE:
            if n & 1 == 0 and not (w & 0x0100):
                e = n
                v = w & 0x7F
                v = v - 128 if v & 0x40 else v
                if w & 0x80:
                    a = self.er(e)
                    self.set_er(e, self._f16(a + v, a, v & 0xFFFF, False))
                else:
                    self.set_er(e, v & 0xFFFF)
                    self.Z = 1 if (v & 0xFFFF) == 0 else 0
                    self.S = 1 if v < 0 else 0
                self.pc = nxt
                return
            if w >> 8 == 0xE9:
                self.psw = i8
                self.pc = nxt
                return
            if w >> 8 == 0xE1:
                self.sp += sx8(i8)
                self.pc = nxt
                return
            if w in (0xED08, 0xEBF7):
                self.pc = nxt
                return
            if w == 0xED80: self.C = 1; self.pc = nxt; return
            if w == 0xEB7F: self.C = 0; self.pc = nxt; return
        if hi == 0xF:
            if self._f_group(w, nxt):
                return
        raise Halt('unknown opcode %04X at %04X' % (w, pc))

    def _shift(self, kind, n, amt):
        """Per manual 3-96..3-100: SLLC/SRLC operate on the 16-bit pair (Rn:Rn-1 / Rn+1:Rn)
        and store only Rn; chains must run SLLC high->low ending with SLL, SRLC low->high
        ending with SRL. Register shift counts use only the low 3 bits."""
        amt &= 7
        if amt == 0:
            return
        a = self.r[n]
        if kind == 0xA:       # SLL
            self.C = (a >> (8 - amt)) & 1
            self.r[n] = (a << amt) & 0xFF
        elif kind == 0xC:     # SRL
            self.C = (a >> (amt - 1)) & 1
            self.r[n] = a >> amt
        elif kind == 0xE:     # SRA
            sa = a - 256 if a & 0x80 else a
            self.C = (sa >> (amt - 1)) & 1
            self.r[n] = (sa >> amt) & 0xFF
        elif kind == 0xB:     # SLLC: pair (Rn : Rn-1)
            v = (a << 8) | self.r[(n - 1) & 15]
            self.C = (v >> (16 - amt)) & 1
            self.r[n] = ((v << amt) >> 8) & 0xFF
        elif kind == 0xD:     # SRLC: pair (Rn+1 : Rn)
            v = (self.r[(n + 1) & 15] << 8) | a
            self.C = (v >> (amt - 1)) & 1
            self.r[n] = (v >> amt) & 0xFF

    def _loadstore(self, w, nxt):
        hi = w >> 12
        n = (w >> 8) & 0xF
        m = (w >> 4) & 0xF
        lo = w & 0xF
        pc = self.pc
        st = lo & 1
        if hi == 9:
            kind = lo >> 1   # 0=Rn 1(=2,3)=ERn  (lo 0/1 -> Rn, 2/3 -> ERn)
            if lo in (0, 1, 2, 3) and (m & 1 == 0 or m in (0x1, 0x3, 0x5) and False):
                size = 1 if lo < 2 else 2
                if m == 0x3 or m == 0x5:
                    pass
            # Dadr
            if m == 0x1 and lo in (0, 1, 2, 3):
                addr = self.fetch16(pc + 2)
                self._access(n, lo, addr)
                self.pc = pc + 4
                return True
            if m == 0x3 and lo in (0, 1, 2, 3):
                self._access(n, lo, self.ea)
                self.pc = nxt
                return True
            if m == 0x5 and lo in (0, 1, 2, 3):
                self._access(n, lo, self.ea)
                self.ea += 1 if lo < 2 else 2
                self.pc = nxt
                return True
            if lo in (0, 1, 2, 3) and (m & 1) == 0:
                base = self.er(m)
                self._access(n, lo, base)
                self.pc = nxt
                return True
            if lo in (8, 9) and (m & 1) == 0:
                d = self.fetch16(pc + 2)
                self._access(n, lo - 8, (self.er(m) + d) & 0xFFFF)
                self.pc = pc + 4
                return True
        if hi == 0xA and lo in (8, 9) and (m & 1) == 0 and (n & 1) == 0:
            d = self.fetch16(pc + 2)
            self._access(n, 2 + (lo - 8), (self.er(m) + d) & 0xFFFF)
            self.pc = pc + 4
            return True
        return False

    def _access(self, n, op, addr):
        # op: 0 L Rn, 1 ST Rn, 2 L ERn, 3 ST ERn
        if op == 0:
            self.r[n] = self.rd(addr); self._zs(self.r[n])
        elif op == 1:
            self.wr(addr, self.r[n])
        elif op == 2:
            v = self.rd16(addr); self.set_er(n, v)
            self.Z = 1 if v == 0 else 0; self.S = v >> 15
        else:
            self.wr16(addr, self.er(n))

    def _ctrl_a(self, w, nxt):
        n = (w >> 8) & 0xF
        m = (w >> 4) & 0xF
        lo = w & 0xF
        # bit ops: 1010_nnnn_0bbb_00xx  /  1010_0000_1bbb_00xx + Dadr
        if lo in (0, 1, 2) and (m & 8) == 0:
            b = m & 7
            if lo == 0: self.r[n] |= (1 << b)
            elif lo == 2: self.r[n] &= ~(1 << b) & 0xFF
            else: self.Z = 0 if (self.r[n] >> b) & 1 else 1
            self.pc = nxt
            return True
        if lo in (0, 1, 2) and (m & 8) and n == 0:
            b = m & 7
            addr = self.fetch16(self.pc + 2)
            v = self.rd(addr)
            if lo == 0: self.wr(addr, v | (1 << b))
            elif lo == 2: self.wr(addr, v & ~(1 << b))
            else: self.Z = 0 if (v >> b) & 1 else 1
            self.pc = self.pc + 4
            return True
        if w & 0xFF0F == 0xA00B:
            self.psw = self.r[m]; self.pc = nxt; return True
        if lo == 3 and (m == 0):
            self.r[n] = self.psw; self.pc = nxt; return True
        if lo == 0xA and m == 1 and (n & 1) == 0 and False:
            pass
        if w & 0xF0FF == 0xA01A:
            self.set_er(n, self.sp); self.pc = nxt; return True
        if w & 0xFF0F == 0xA10A:
            self.sp = self.er(m); self.pc = nxt; return True
        return False

    def _f_group(self, w, nxt):
        n = (w >> 8) & 0xF
        m = (w >> 4) & 0xF
        lo = w & 0xF
        pc = self.pc
        if w == 0xFFFF:
            raise Halt('BRK at %04X' % pc)
        if w == 0xFE8F: self.pc = nxt; return True
        if w == 0xFE1F: self.pc = self.lr; self.csr = self.lcsr; return True
        if w == 0xFE0F: self.pc = self.lr; self.csr = self.lcsr; return True
        if w == 0xFECF: self.C ^= 1; self.pc = nxt; return True
        if w == 0xFE2F or w == 0xFE3F:
            v = self.rd(self.ea)
            d = 1 if w == 0xFE2F else -1
            self.wr(self.ea, self._f8(v + d, v, 1 if d > 0 else 1, d < 0))
            self.pc = nxt; return True
        if w & 0xFF0F == 0xF00A:
            self.ea = self.er(m); self.pc = nxt; return True
        if w & 0xFF0F == 0xF00B:
            self.ea = (self.er(m) + self.fetch16(pc + 2)) & 0xFFFF; self.pc = pc + 4; return True
        if w == 0xF00C:
            self.ea = self.fetch16(pc + 2); self.pc = pc + 4; return True
        # B/BL Cadr
        if (w & 0x00FF) in (0x00, 0x01):
            tgt = self.fetch16(pc + 2)
            if w & 1:
                self.lr = pc + 4
                self.lcsr = self.csr
            if tgt == pc and n == self.csr and (w & 1) == 0:
                raise Halt('halt: B to self at %04X' % pc)
            self.pc = tgt
            self.csr = n
            return True
        if w & 0xFF0F == 0xF002:
            self.pc = self.er(m); return True
        if w & 0xFF0F == 0xF003:
            self.lr = nxt; self.lcsr = self.csr; self.pc = self.er(m); return True
        # MUL / DIV
        if lo == 4 and (n & 1) == 0:
            self.set_er(n, self.r[n] * self.r[m] & 0xFFFF)
            self.Z = 1 if self.er(n) == 0 else 0
            self.pc = nxt; return True
        if lo == 9 and (n & 1) == 0:
            d = self.r[m]
            a = self.er(n)
            if d == 0:
                self.set_er(n, 0xFFFF); self.C = 1
            else:
                self.set_er(n, a // d); self.r[m] = a % d; self.C = 0
            self.Z = 1 if self.er(n) == 0 else 0
            self.pc = nxt; return True
        if lo == 5 and (n & 1) == 0 and (m & 1) == 0:   # MOV ERn,ERm
            v = self.er(m); self.set_er(n, v)
            self.Z = 1 if v == 0 else 0; self.S = v >> 15
            self.pc = nxt; return True
        if lo == 6 and (n & 1) == 0 and (m & 1) == 0:   # ADD ERn,ERm
            a, b = self.er(n), self.er(m)
            self.set_er(n, self._f16(a + b, a, b, False)); self.pc = nxt; return True
        if lo == 7 and (n & 1) == 0 and (m & 1) == 0:   # CMP ERn,ERm
            a, b = self.er(n), self.er(m)
            self._f16(a - b, a, b, True); self.pc = nxt; return True
        # PUSH/POP
        if w & 0x00FF == 0x00CE:   # PUSH list
            bits = n
            if bits & 1: self.push_bytes([self.ea & 0xFF, self.ea >> 8])
            if bits & 8: self.push_bytes([self.lr & 0xFF, self.lr >> 8, self.lcsr, 0])
            self.pc = nxt; return True
        if w & 0x00FF == 0x008E:   # POP list
            bits = n
            if bits & 1:
                b = self.pop_bytes(2); self.ea = b[0] | (b[1] << 8)
            if bits & 8:
                b = self.pop_bytes(4); self.lr = b[0] | (b[1] << 8); self.lcsr = b[2]
            if bits & 2:
                self.pc = self.lr if (bits & 8) else None
                if self.pc is None:
                    # POP PC alone: PC <- ELR slot; the compiler always pairs it
                    # with the 4-byte frame pushed by PUSH LR
                    b = self.pop_bytes(4); self.pc = b[0] | (b[1] << 8); self.csr = b[2]
                else:
                    self.csr = self.lcsr
                return True
            self.pc = nxt; return True
        if lo == 0xE and m == 5:   # PUSH ERn
            v = self.er(n); self.push_bytes([v & 0xFF, v >> 8]); self.pc = nxt; return True
        if lo == 0xE and m == 1:   # POP ERn
            b = self.pop_bytes(2); self.set_er(n, b[0] | (b[1] << 8)); self.pc = nxt; return True
        if lo == 0xE and m == 4:   # PUSH Rn
            self.push_bytes([self.r[n]]); self.pc = nxt; return True
        if lo == 0xE and m == 0:   # POP Rn
            self.r[n] = self.pop_bytes(1)[0]; self.pc = nxt; return True
        return False

    def run(self, max_steps=5_000_000):
        try:
            while self.steps < max_steps:
                self.step()
        except Halt as h:
            return str(h)
        return 'max steps'

    # ---- LCD dump ----
    def pixel(self, x, y):
        """state of screen pixel (x, y) in USER coordinates (cw: VRAM row y + 1)"""
        y += self.tg.row_off
        if y >= 64:
            return 0
        a = y * 32 + (x >> 3)
        bit = 7 - (x & 7)
        lo = (self.planes[0][a] >> bit) & 1
        hi = (self.planes[1][a] >> bit) & 1
        if self.tg.planes == 1:
            return 3 if lo else 0
        return (hi << 1) | lo

    def ascii(self):
        rows = []
        for y in range(self.tg.rows):
            rows.append(''.join(' .+#'[self.pixel(x, y)] for x in range(192)))
        return '\n'.join(rows)
