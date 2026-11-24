// prelude_shapes.hpp - CWiz C-subset port of the upstream Wiz drawing PRELUDE
// helpers missing from PRELUDE_SRC in compiler/src/cwiz.cpp.  Bodies are
// translated from the authoritative PRELUDE dict in upstream/wizc.py
// (pkdev-en/Wiz @ 3a4308a), keys: ellipse, fillellipse, tri, filltri,
// roundrect, clip, charz, textz, numz, draw, sprite_rot; cross-checked against
// docs/wiz-guide.md.  compiler/src/wiz.cpp is only a secondary cross-check.
//
// Adaptations to CWiz (not source changes): upstream clip() pokes the clip
// window into runtime bytes 0xE801..0xE804; here it writes the four int
// globals declared below, which the clip-aware hline()/vline() in
// shapes-hline.txt read.  Upstream's sprite() from the 'sprite_rot' key keeps
// its requested CWiz name sprite_rot (existing sprite() is not redefined).
//
// Add PRELUDE_SHAPES to prelude_source() (or concatenate it onto PRELUDE_SRC)
// so select_prelude() can pull these declarations in on demand.  Every helper
// is a named top-level declaration; the existing pixel/line/hline/vline/_font
// prelude declarations are reused, never redefined.  Do not declare the four
// _clip_* globals a second time in the integrator's copy of PRELUDE_SRC.

#ifndef WIZ_PRELUDE_SHAPES_HPP
#define WIZ_PRELUDE_SHAPES_HPP

static const char* PRELUDE_SHAPES = R"CWIZ(
// ---- clip window -------------------------------------------------------
// Full-screen bounds by default; clip() rewrites all four of them.
int _clip_x0 = 0;
int _clip_x1 = 192;
int _clip_y0 = 0;
int _clip_y1 = _ROWS;

// Upstream PRELUDE['clip'] bounds x1 at 192 and y1 at 64 (wiz.cpp's
// @ROWS@ spelling is not authoritative here).
void clip(int x = 0, int y = 0, int w = 192, int h = 64) {
    int x1 = x + w;
    int y1 = y + h;
    if (x < 0) { x = 0; }
    if (y < 0) { y = 0; }
    if (x1 > 192) { x1 = 192; }
    if (y1 > 64) { y1 = 64; }
    _clip_x0 = x;
    _clip_x1 = x1;
    _clip_y0 = y;
    _clip_y1 = y1;
}

// ---- ellipse -----------------------------------------------------------
void ellipse(int cx, int cy, int rx, int ry, int c = 1) {
    int x = rx;
    int y = 0;
    int rx2 = rx * rx;
    int ry2 = ry * ry;
    int dx2 = ry2 * (1 - rx - rx);
    int dy2 = rx2;
    int err2 = 0;
    int sx2 = ry2 * rx;
    int sy2 = 0;
    while (sx2 >= sy2) {
        pixel(cx + x, cy + y, c);
        pixel(cx - x, cy + y, c);
        pixel(cx - x, cy - y, c);
        pixel(cx + x, cy - y, c);
        sy2 = sy2 + rx2;
        err2 = err2 + dy2;
        dy2 = dy2 + rx2 + rx2;
        y = y + 1;
        if (err2 + err2 + dx2 > 0) {
            sx2 = sx2 - ry2;
            err2 = err2 + dx2;
            dx2 = dx2 + ry2 + ry2;
            x = x - 1;
        }
    }
    x = 0;
    y = ry;
    dx2 = ry2;
    dy2 = rx2 * (1 - ry - ry);
    err2 = 0;
    sy2 = rx2 * ry;
    sx2 = 0;
    while (sx2 <= sy2) {
        pixel(cx + x, cy + y, c);
        pixel(cx - x, cy + y, c);
        pixel(cx - x, cy - y, c);
        pixel(cx + x, cy - y, c);
        sx2 = sx2 + ry2;
        err2 = err2 + dx2;
        dx2 = dx2 + ry2 + ry2;
        x = x + 1;
        if (err2 + err2 + dy2 > 0) {
            sy2 = sy2 - rx2;
            err2 = err2 + dy2;
            dy2 = dy2 + rx2 + rx2;
            y = y - 1;
        }
    }
}

void fillellipse(int cx, int cy, int rx, int ry, int c = 1) {
    int ry2 = ry * ry;
    int rx2 = rx * rx;
    int j = 0 - ry;
    while (j <= ry) {
        int w = rx2 - rx2 * j * j / ry2;
        if (w < 0) { w = 0; }
        int xw = 0;
        while (xw * xw <= w) { xw = xw + 1; }
        hline(cx - xw, cx + xw, cy + j, c);
        j = j + 1;
    }
}

// ---- triangles ---------------------------------------------------------
void tri(int x0, int y0, int x1, int y1, int x2, int y2, int c = 1) {
    line(x0, y0, x1, y1, c);
    line(x1, y1, x2, y2, c);
    line(x2, y2, x0, y0, c);
}

void filltri(int x0, int y0, int x1, int y1, int x2, int y2, int c = 1) {
    int ax = x0;
    int ay = y0;
    int bx = x1;
    int by = y1;
    int ex = x2;
    int ey = y2;
    int tm = 0;
    if (ay > by) {
        tm = ax; ax = bx; bx = tm;
        tm = ay; ay = by; by = tm;
    }
    if (ay > ey) {
        tm = ax; ax = ex; ex = tm;
        tm = ay; ay = ey; ey = tm;
    }
    if (by > ey) {
        tm = bx; bx = ex; ex = tm;
        tm = by; by = ey; ey = tm;
    }
    int h = ey - ay;
    if (h == 0) { return; }
    int y = ay;
    while (y <= ey) {
        int xa = ax + (ex - ax) * (y - ay) / h;
        int xb;
        if (y < by) { xb = ax + (bx - ax) * (y - ay) / (by - ay + 1); }
        else { xb = bx + (ex - bx) * (y - by) / (ey - by + 1); }
        if (xa > xb) { tm = xa; xa = xb; xb = tm; }
        hline(xa, xb + 1, y, c);
        y = y + 1;
    }
}

// ---- rounded rectangle -------------------------------------------------
void roundrect(int x, int y, int w, int h, int r, int c = 1) {
    hline(x + r, x + w - r, y, c);
    hline(x + r, x + w - r, y + h - 1, c);
    vline(x, y + r, y + h - r, c);
    vline(x + w - 1, y + r, y + h - r, c);
    int cx0 = x + r;
    int cy0 = y + r;
    int cx1 = x + w - 1 - r;
    int cy1 = y + h - 1 - r;
    int xc = r;
    int yc = 0;
    int er2 = 1 - r;
    while (xc >= yc) {
        pixel(cx1 + xc, cy1 + yc, c);
        pixel(cx1 + yc, cy1 + xc, c);
        pixel(cx0 - yc, cy1 + xc, c);
        pixel(cx0 - xc, cy1 + yc, c);
        pixel(cx0 - xc, cy0 - yc, c);
        pixel(cx0 - yc, cy0 - xc, c);
        pixel(cx1 + yc, cy0 - xc, c);
        pixel(cx1 + xc, cy0 - yc, c);
        yc = yc + 1;
        if (er2 < 0) { er2 = er2 + yc + yc + 1; }
        else { xc = xc - 1; er2 = er2 + (yc - xc) * 2 + 1; }
    }
}

// ---- scaled text (char is a keyword in CWiz, hence charz) --------------
void charz(int x, int y, int c, int h, int w) {
    if (c < 32 || c > 126) { c = 63; }
    int base = (c - 32) * 8;
    int sr = 0;
    int er = 0;
    int r = 0;
    while (r < h) {
        int bits = _font[base + sr];
        int sc = 0;
        int ec = 0;
        int m = 128;
        int k = 0;
        while (k < w) {
            pixel(x + k, y + r, (bits & m) != 0);
            ec = ec + 6;
            while (ec >= w) { ec = ec - w; sc = sc + 1; m = m >> 1; }
            k = k + 1;
        }
        er = er + 8;
        while (er >= h) { er = er - h; sr = sr + 1; }
        r = r + 1;
    }
}

int textz(int x, int y, int h, int w, int s) {
    int c = _rpeek(s);
    while (c != 0) {
        charz(x, y, c, h, w);
        x = x + w;
        s = s + 1;
        c = _rpeek(s);
    }
    return x;
}

int numz(int x, int y, int h, int w, int n) {
    if (n == -32768) { return textz(x, y, h, w, "-32768"); }
    if (n < 0) { charz(x, y, 45, h, w); x = x + w; n = 0 - n; }
    int d = 10000;
    int shown = 0;
    while (d > 0) {
        int q = n / d;
        n = n - q * d;
        if (q > 0 || shown || d == 1) {
            charz(x, y, 48 + q, h, w);
            x = x + w;
            shown = 1;
        }
        d = d / 10;
    }
    return x;
}

// ---- hex-encoded bitmap ------------------------------------------------
int _hexv(int c) {
    if (c > 57) { return (c | 32) - 87; }
    return c - 48;
}

// s is the address of ROM bytes (a CWiz string literal is a ROM address).
void draw(int x, int y, int h, int w, int s) {
    int bw = (w + 7) >> 3;
    int r = 0;
    while (r < h) {
        int k = 0;
        while (k < w) {
            int i = (r * bw + (k >> 3)) * 2 + ((k & 4) >> 2);
            if (_hexv(_rpeek(s + i)) & (8 >> (k & 3))) { pixel(x + k, y + r); }
            k = k + 1;
        }
        r = r + 1;
    }
}

// ---- rotated / flipped sprite ------------------------------------------
void _sprite_px(int sx, int sy, int pw, int ph, int data, int rot, int flipx, int flipy) {
    int bw = (pw + 7) / 8;
    int py = 0;
    while (py < ph) {
        int px = 0;
        while (px < pw) {
            int byte_idx = py * bw + px / 8;
            int bit_pos = 7 - (px & 7);
            int bval = _rpeek(data + byte_idx);
            if ((bval >> bit_pos) & 1) {
                int rx = px;
                int ry = py;
                if (flipx) { rx = pw - 1 - px; }
                if (flipy) { ry = ph - 1 - py; }
                if (rot == 0) { pixel(sx + rx, sy + ry); }
                else if (rot == 1) { pixel(sx + ph - 1 - ry, sy + rx); }
                else if (rot == 2) { pixel(sx + pw - 1 - rx, sy + ph - 1 - ry); }
                else if (rot == 3) { pixel(sx + ry, sy + pw - 1 - rx); }
            }
            px = px + 1;
        }
        py = py + 1;
    }
}

void sprite_rot(int x, int y, int pw, int ph, int data, int w = 1, int h = 1, int rot = 0) {
    int fpw = pw;
    int fph = ph;
    int fx = 0;
    int fy = 0;
    if (fpw < 0) { fx = 1; fpw = 0 - fpw; }
    if (fph < 0) { fy = 1; fph = 0 - fph; }
    _sprite_px(x, y, fpw, fph, data, rot, fx, fy);
}
)CWIZ";

#endif  // WIZ_PRELUDE_SHAPES_HPP
