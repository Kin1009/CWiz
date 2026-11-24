// prelude_heap_rtc.hpp - CWiz C-subset port of the upstream Wiz PRELUDE
// entries for the 16-bit word helpers, dynamic heap, RAM file system,
// BCD/date helpers, RTC read/set/display and the elapsed-time helper.
//
// Kept as a standalone raw string so the prelude aggregator can concatenate it
// with the existing PRELUDE_SRC; every helper is a named top-level declaration
// (including _snap and _merge) so select_prelude() dependency resolution sees
// them.  Nothing in here uses Python syntax or nested functions.
//
// Hardware notes (also in the port report):
//   * Heap headers are read from the runtime-initialised words at 0xE818
//     (free-list head), 0xE81A (hi) and 0xE81C (file-list head).  The first
//     free block header at 0xE820 comes from that word pair, so the allocator
//     never hardcodes the heap start.
//   * RTC registers are peek/poke 0xF0C0..0xF0C7.  0xF0C3 is an immutable
//     SFR address for allocation purposes, but reading/writing it is exactly
//     what upstream does; the behaviour is preserved verbatim here.
#pragma once

static const char* PRELUDE_HEAP_RTC = R"CWIZ(
// ---- 16-bit word helpers (little-endian) -----------------------------
int _rdw(int a) { return peek(a) + peek(a + 1) * 256; }
void _wrw(int a, int v) { poke(a, v & 255); poke(a + 1, (v >> 8) & 255); }

// ---- dynamic heap ----------------------------------------------------
// The runtime seeds 0xE818/0xE81A with __HEAP_LO/__HEAP_HI and puts a single
// free block header at __HEAP_LO.  A header's sign bit marks a free block.
int alloc(int n) {
    if (n < 1) { return 0; }
    int need = n;
    if (need & 1) { need = need + 1; }
    need = need + 2;
    int p = _rdw(0xE818);
    int hi = _rdw(0xE81A);
    while (p < hi) {
        int h = _rdw(p);
        int sz = h & 0x7FFF;
        if (h < 0 && sz >= need) {
            if (sz - need >= 4) {
                _wrw(p, need);
                _wrw(p + need, (sz - need) | 0x8000);
            } else {
                _wrw(p, sz);
            }
            return p + 2;
        }
        p = p + sz;
    }
    return 0;
}

void _merge() {
    int q = _rdw(0xE818);
    int hi = _rdw(0xE81A);
    while (q < hi) {
        int h = _rdw(q);
        if (h < 0) {
            int sz = h & 0x7FFF;
            int n2 = q + sz;
            while (n2 < hi) {
                int h2 = _rdw(n2);
                if (h2 >= 0) { break; }
                sz = sz + (h2 & 0x7FFF);
                n2 = q + sz;
            }
            _wrw(q, sz | 0x8000);
            q = q + sz;
        } else {
            q = q + h;
        }
    }
}

void free(int p) {
    if (p == 0) { return; }
    _wrw(p - 2, _rdw(p - 2) - 32768);
    _merge();
}

// ---- RAM file system -------------------------------------------------
// nm is a numeric name: the ROM address of a <=12-byte NUL-terminated name.
void _cpy(char *dst, char *src, int n) {
    int i = 0;
    while (i < n) { poke(dst + i, peek(src + i)); i = i + 1; }
}
int _feq(int nm, int node) {
    int k = 0;
    while (k < 12) {
        int c = _rpeek(nm + k);
        if (c != peek(node + k)) { return 0; }
        if (c == 0) { return 1; }
        k = k + 1;
    }
    return 1;
}
int _fnode(int nm) {
    int p = _rdw(0xE81C);
    while (p != 0) {
        if (_feq(nm, p)) { return p; }
        p = _rdw(p + 12);
    }
    return 0;
}
int _fsnew(int nm) {
    int node = alloc(18);
    if (node == 0) { return 0; }
    int k = 0;
    while (k < 12) { poke(node + k, 0); k = k + 1; }
    k = 0;
    while (k < 12) {
        int c = _rpeek(nm + k);
        if (c == 0) { break; }
        poke(node + k, c);
        k = k + 1;
    }
    if (k == 12) { free(node); return 0; }
    _wrw(node + 12, _rdw(0xE81C));
    _wrw(0xE81C, node);
    _wrw(node + 14, 0);
    _wrw(node + 16, 0);
    return node;
}
int f_save(int nm, char *buf, int n) {
    if (n < 0) { return 0; }
    int node = _fnode(nm);
    if (node == 0) {
        node = _fsnew(nm);
        if (node == 0) { return 0; }
    }
    int old = _rdw(node + 14);
    if (old != 0) { free(old); }
    _wrw(node + 14, 0);
    _wrw(node + 16, 0);
    if (n == 0) { return 1; }
    int data = alloc(n);
    if (data == 0) { return 0; }
    _cpy(data, buf, n);
    _wrw(node + 14, data);
    _wrw(node + 16, n);
    return 1;
}
int f_load(int nm, char *buf, int max) {
    int node = _fnode(nm);
    if (node == 0) { return -1; }
    int n = _rdw(node + 16);
    if (n > max) { n = max; }
    int data = _rdw(node + 14);
    if (data != 0 && n > 0) { _cpy(buf, data, n); }
    return n;
}
int f_exists(int nm) {
    if (_fnode(nm) != 0) { return 1; }
    return 0;
}
int f_remove(int nm) {
    int prev = 0;
    int p = _rdw(0xE81C);
    while (p != 0) {
        if (_feq(nm, p)) {
            int nxt = _rdw(p + 12);
            if (prev == 0) { _wrw(0xE81C, nxt); }
            else { _wrw(prev + 12, nxt); }
            int d = _rdw(p + 14);
            if (d != 0) { free(d); }
            free(p);
            return 1;
        }
        prev = p;
        p = _rdw(p + 12);
    }
    return 0;
}

// ---- BCD / date helpers ----------------------------------------------
int _dec(int b) { return ((b >> 4) & 15) * 10 + (b & 15); }
int _bcd(int v) { return ((v / 10) << 4) | (v % 10); }
int _weekday(int y, int mo, int d) {
    if (mo < 3) { mo = mo + 12; y = y - 1; }
    int k = y % 100;
    int j = y / 100;
    int h = (d + (13 * (mo + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    return (h + 6) % 7;
}

// ---- RTC read / set ---------------------------------------------------
int hour() { return _dec(peek(0xF0C2)); }
int minute() { return _dec(peek(0xF0C1)); }
int second() { return _dec(peek(0xF0C0)); }
int day() { return peek(0xF0C3); }
int weekday() { return peek(0xF0C4); }
int month() { return peek(0xF0C5); }
int year() { return peek(0xF0C6) + 2026; }

int _rtc[7];
void _snap() {
    _rtc[0] = hour();
    _rtc[1] = minute();
    _rtc[2] = second();
    _rtc[3] = day();
    _rtc[4] = weekday();
    _rtc[5] = month();
    _rtc[6] = year();
}
// Double-snapshot read: retry until two consecutive snapshots agree.
void rtc_read() {
    _snap();
    while (1) {
        int a0 = _rtc[0];
        int a1 = _rtc[1];
        int a2 = _rtc[2];
        int a3 = _rtc[3];
        int a4 = _rtc[4];
        int a5 = _rtc[5];
        int a6 = _rtc[6];
        _snap();
        if (a0 == _rtc[0] && a1 == _rtc[1] && a2 == _rtc[2] && a3 == _rtc[3] &&
            a4 == _rtc[4] && a5 == _rtc[5] && a6 == _rtc[6]) {
            return;
        }
    }
}
int rtc_alive() {
    rtc_enable();
    int a = second();
    int t = ticks();
    while (elapsed(t) < 20000) {
        if (second() != a) { return 1; }
    }
    return 0;
}
void set_hours(int h) { poke(0xF0C2, _bcd(h)); }
void set_minutes(int m) { poke(0xF0C1, _bcd(m)); }
void set_seconds(int s) { poke(0xF0C0, _bcd(s)); }
void set_day(int d) { poke(0xF0C3, d); }
void set_month(int mo) { poke(0xF0C5, mo); }
void set_year(int y) { poke(0xF0C6, y - 2026); }
void set_time(int h, int m, int s) {
    poke(0xF0C0, _bcd(s));
    poke(0xF0C1, _bcd(m));
    poke(0xF0C2, _bcd(h));
}
void set_date(int y, int mo, int d) {
    poke(0xF0C6, y - 2026);
    poke(0xF0C5, mo);
    poke(0xF0C3, d);
    poke(0xF0C4, _weekday(y, mo, d));
}
void rtc_enable() { poke(0xF0C7, 1); }
void rtc_disable() { poke(0xF0C7, 0); }
void rtc_reset() {
    rtc_disable();
    poke(0xF0C0, 0);
    poke(0xF0C1, 0);
    poke(0xF0C2, 0);
    poke(0xF0C3, 1);
    poke(0xF0C4, 1);
    poke(0xF0C5, 1);
    poke(0xF0C6, 0);
}

// ---- RTC display ------------------------------------------------------
void draw_time(int x, int y) {
    rtc_read();
    num(x, y, _rtc[0] / 10);
    num(x + 8, y, _rtc[0] % 10);
    text(x + 18, y, ":");
    num(x + 26, y, _rtc[1] / 10);
    num(x + 34, y, _rtc[1] % 10);
    text(x + 44, y, ":");
    num(x + 52, y, _rtc[2] / 10);
    num(x + 60, y, _rtc[2] % 10);
}
void draw_date(int x, int y) {
    rtc_read();
    num(x, y, _rtc[3] / 10);
    num(x + 8, y, _rtc[3] % 10);
    text(x + 18, y, "/");
    num(x + 26, y, _rtc[5] / 10);
    num(x + 34, y, _rtc[5] % 10);
    text(x + 44, y, "/");
    num(x + 52, y, (_rtc[6] % 100) / 10);
    num(x + 60, y, _rtc[6] % 10);
}

// ---- timer ------------------------------------------------------------
int elapsed(int t) { return ticks() - t; }
)CWIZ";
