// prelude_input.hpp - CWiz C-subset port of the upstream Wiz input/string PRELUDE.
//
// The raw string below is fed to the CWiz parser as extra prelude source (next
// to PRELUDE_SRC in cwiz.cpp).  It is deliberately written in the same C
// idiom as that prelude: no Python syntax, no variadics, no nested functions,
// one named top-level declaration per helper so select_prelude() can pull in
// exactly the dependencies a user program needs.
//
// Translation sources: compiler/src/wiz.cpp (Python PRELUDE entries
// "getc", "input_num", "getc_tap", "input_tap", "input_pad") and
// upstream/wizc.py + docs/wiz-guide.md.  wait_key(), strlen/strcmp etc.
// already live in cwiz.cpp and are reused, never redefined.
#ifndef WIZ_PRELUDE_INPUT_HPP
#define WIZ_PRELUDE_INPUT_HPP

static const char* PRELUDE_INPUT = R"CWIZ(
// ---- waitkey(ms): bounded wait for a new key press ---------------------
// Paced by delay_ms(1) (not ticks()) so the timer is re-armed the same way
// as __delay() on real hardware.  _wkl suppresses the repeat of a key that
// was already down when the wait started.
int _wkl = -1;

int waitkey(int ms) {
    if (ms < 0) { ms = 0; }
    int k = -1;
    while (ms > 0) {
        k = key();
        if (k != _wkl) {
            _wkl = k;
            if (k != -1) { return k; }
        }
        delay_ms(1);
        ms = ms - 1;
    }
    k = key();
    if (k != _wkl) {
        _wkl = k;
        if (k != -1) { return k; }
    }
    return -1;
}

// ---- getc(): blocking key read ----------------------------------------
int getc(void) {
    return wait_key();
}

// ---- input_num(x, y, default): numeric field --------------------------
int _dig(int k) {
    if (k == K_0) { return 0; }
    if (k == K_1) { return 1; }
    if (k == K_2) { return 2; }
    if (k == K_3) { return 3; }
    if (k == K_4) { return 4; }
    if (k == K_5) { return 5; }
    if (k == K_6) { return 6; }
    if (k == K_7) { return 7; }
    if (k == K_8) { return 8; }
    if (k == K_9) { return 9; }
    return -1;
}

int input_num(int x, int y, int d) {
    int n5 = d;
    int sg5 = 0;
    if (n5 < 0) {
        sg5 = 1;
        n5 = 0 - n5;
        if (n5 < 0) { n5 = 32767; }
    }
    int edit5 = 0;
    int m5 = peek(0xE800);
    while (1) {
        clear_rect(x, y, 80, 10);
        if (sg5 && n5 != 0) {
            glyph(x, y, 45);
            num(x + 6, y, n5);
        } else {
            num(x, y, n5);
        }
        if (m5) { flip(); }
        int k5 = wait_key();
        if (k5 == K_AC) { return -32768; }
        if (k5 == K_EXE) {
            if (sg5) { return 0 - n5; }
            return n5;
        }
        if (k5 == K_MINUS || k5 == K_NEG) {
            sg5 = 1 - sg5;
            edit5 = 1;
        } else if (k5 == K_DEL) {
            n5 = n5 / 10;
            edit5 = 1;
        } else {
            int d5 = _dig(k5);
            if (d5 >= 0) {
                if (edit5 == 0 || n5 == 0) {
                    n5 = d5;
                } else if (n5 < 3276) {
                    n5 = n5 * 10 + d5;
                } else if (n5 == 3276 && d5 < 8) {
                    n5 = n5 * 10 + d5;
                }
                edit5 = 1;
            }
        }
    }
}

// ---- getc_tap(): T9 multi-tap decoder --------------------------------
// ticks() runs at 8 ticks/ms: 800 ticks = 100ms cycles a held key,
// 6400 ticks = 800ms commits the pending character on release.
// AC -> -32768, EXE -> -2, DEL -> -3.  An EXE/DEL arriving while a letter
// is pending commits that letter and queues the control code in _gc_pend
// for the next call, so no typed character is lost.
int _gc_pend = 0;

// ms = key code, mode = tap index (upstream _tap_char(k, n)).
int _tap_char(int ms, int mode) {
    if (ms == K_1) { return 46 + mode % 4; }
    if (ms == K_2) { return 65 + mode % 3; }
    if (ms == K_3) { return 68 + mode % 3; }
    if (ms == K_4) { return 71 + mode % 3; }
    if (ms == K_5) { return 74 + mode % 3; }
    if (ms == K_6) { return 77 + mode % 3; }
    if (ms == K_7) { return 80 + mode % 4; }
    if (ms == K_8) { return 84 + mode % 3; }
    if (ms == K_9) { return 87 + mode % 4; }
    if (ms == K_0) { return 32; }
    return 63;
}

int getc_tap(void) {
    if (_gc_pend) {
        int c7 = _gc_pend;
        _gc_pend = 0;
        return c7;
    }
    int last_k = -1;
    int tap_n = 0;
    int tap_t = 0;
    while (1) {
        int k6 = key();
        if (k6 == K_AC) { return -32768; }
        if (k6 == K_EXE || k6 == K_DEL) {
            if (last_k != -1) {
                _gc_pend = -2;
                if (k6 == K_DEL) { _gc_pend = -3; }
                return _tap_char(last_k, tap_n);
            }
            if (k6 == K_EXE) { return -2; }
            return -3;
        }
        if (k6 != -1 && k6 != last_k) {
            if (last_k != -1) { return _tap_char(last_k, tap_n); }
            last_k = k6;
            tap_n = 0;
            tap_t = ticks();
        } else if (k6 == last_k) {
            if (ticks() - tap_t > 800) {
                tap_n = tap_n + 1;
                tap_t = ticks();
            }
        } else if (k6 == -1 && last_k != -1) {
            if (ticks() - tap_t > 6400) {
                int c6 = _tap_char(last_k, tap_n);
                last_k = -1;
                tap_n = 0;
                return c6;
            }
        }
    }
}

// ---- input_tap(b, max, x, y): multi-tap string field ------------------
// Writes at most max-1 bytes plus the terminating NUL.  AC cancels (-1 and
// an empty buffer), EXE commits (length), DEL backspaces.  The release
// gate at entry keeps the key that opened the screen from confirming an
// empty string.  flip() only in buffered mode (byte at 0xE800).
int input_tap(char *b, int max, int x, int y) {
    int pos6 = 0;
    int m6 = peek(0xE800);
    poke(b, 0);
    while (key() != -1) { }
    while (1) {
        int c6 = getc_tap();
        if (c6 == -32768) { return -1; }
        if (c6 == -2) { return pos6; }
        if (c6 == -3) {
            if (pos6 > 0) {
                pos6 = pos6 - 1;
                poke(b + pos6, 0);
            }
        } else if (pos6 < max - 1) {
            poke(b + pos6, c6);
            pos6 = pos6 + 1;
            poke(b + pos6, 0);
        }
        clear_rect(x, y, 120, 10);
        text(x, y, b);
        if (m6) { flip(); }
    }
}

// ---- input_pad(b, max, x, y): on-screen keyboard ----------------------
// 4 char rows x 10 cols plus a control row [Aa][SPACE][DEL][OK].  The
// physical digit keys and DOT type straight into the field.  Up/down/left/
// right move the inverted selection (rows wrap), EXE presses it, DEL
// backspaces, ANS or the OK key commit (length), AC cancels (-1).
rom8 char _pad_cs[41] = "abcdefghijklmnopqrstuvwxyz0123456789.,?!";
int _pad_up = 0;

int _pad_ch(int r, int c) {
    int ch = _rpeek(_pad_cs + r * 10 + c);
    if (_pad_up && ch >= 97 && ch <= 122) { ch = ch - 32; }
    return ch;
}

int _pdig(int k) {
    if (k == K_0) { return 48; }
    if (k == K_1) { return 49; }
    if (k == K_2) { return 50; }
    if (k == K_3) { return 51; }
    if (k == K_4) { return 52; }
    if (k == K_5) { return 53; }
    if (k == K_6) { return 54; }
    if (k == K_7) { return 55; }
    if (k == K_8) { return 56; }
    if (k == K_9) { return 57; }
    return 0;
}

void _pad_draw_grid(void) {
    clear_rect(0, 10, 192, 53);
    hline(0, 192, 10);
    hline(0, 192, 21);
    hline(0, 192, 32);
    hline(0, 192, 43);
    hline(0, 192, 54);
    hline(0, 192, 62);
    int c = 0;
    while (c <= 10) { vline(1 + c * 19, 10, 54); c = c + 1; }
    int r = 0;
    while (r < 4) {
        c = 0;
        while (c < 10) {
            glyph(7 + c * 19, 11 + r * 11, _pad_ch(r, c));
            c = c + 1;
        }
        r = r + 1;
    }
    vline(1, 54, 63);
    vline(30, 54, 63);
    vline(131, 54, 63);
    vline(162, 54, 63);
    vline(191, 54, 63);
    if (_pad_up) { text(4, 55, "AA"); }
    else { text(4, 55, "Aa"); }
    text(55, 55, "SPACE");
    text(134, 55, "DEL");
    text(165, 55, "OK");
}

void _pad_sel(int r, int c) {
    if (r < 4) {
        invert_rect(2 + c * 19, 11 + r * 11, 18, 10);
    } else {
        if (c == 0) { invert_rect(2, 55, 28, 8); }
        else if (c == 1) { invert_rect(31, 55, 100, 8); }
        else if (c == 2) { invert_rect(132, 55, 29, 8); }
        else { invert_rect(163, 55, 28, 8); }
    }
}

void _pad_redraw_letters(void) {
    int r = 0;
    while (r < 2) {
        int c = 0;
        while (c < 10) {
            clear_rect(2 + c * 19, 11 + r * 11, 18, 10);
            glyph(7 + c * 19, 11 + r * 11, _pad_ch(r, c));
            c = c + 1;
        }
        r = r + 1;
    }
    clear_rect(2, 55, 28, 8);
    if (_pad_up) { text(4, 55, "AA"); }
    else { text(4, 55, "Aa"); }
}

void _pad_field(char *b, int n, int x, int fw, int m) {
    clear_rect(x + 1, 1, fw - 2, 8);
    text(x + 2, 1, b);
    glyph(x + 2 + n * 6, 1, 95);
    if (m) { flip(); }
}

int input_pad(char *b, int max, int x, int y) {
    _pad_up = 0;
    int pad_r = 0;
    int pad_c = 0;
    int pos7 = 0;
    int m7 = peek(0xE800);
    int fw7 = max * 6 + 8;
    if (fw7 > 190) { fw7 = 190; }
    poke(b, 0);
    while (key() != -1) { }
    clear();
    rect(x, 0, fw7, 10);
    _pad_draw_grid();
    _pad_field(b, 0, x, fw7, 0);
    _pad_sel(0, 0);
    if (m7) { flip(); }
    while (1) {
        int k7 = wait_key();
        if (k7 == K_AC) { return -1; }
        if (k7 == K_ANS) { return pos7; }
        int asc8 = _pdig(k7);
        if (asc8 > 0) {
            if (pos7 < max - 1) {
                poke(b + pos7, asc8);
                pos7 = pos7 + 1;
                poke(b + pos7, 0);
                _pad_field(b, pos7, x, fw7, m7);
            }
        }
        if (k7 == K_DOT) {
            if (pos7 < max - 1) {
                poke(b + pos7, 46);
                pos7 = pos7 + 1;
                poke(b + pos7, 0);
                _pad_field(b, pos7, x, fw7, m7);
            }
        }
        if (k7 == K_DEL) {
            if (pos7 > 0) {
                _pad_sel(pad_r, pad_c);
                pos7 = pos7 - 1;
                poke(b + pos7, 0);
                _pad_field(b, pos7, x, fw7, m7);
                _pad_sel(pad_r, pad_c);
            }
        }
        if (k7 == K_EXE) {
            if (pad_r < 4) {
                if (pos7 < max - 1) {
                    poke(b + pos7, _pad_ch(pad_r, pad_c));
                    pos7 = pos7 + 1;
                    poke(b + pos7, 0);
                    _pad_field(b, pos7, x, fw7, m7);
                }
            } else {
                if (pad_c == 0) {
                    _pad_sel(pad_r, pad_c);
                    _pad_up = 1 - _pad_up;
                    _pad_redraw_letters();
                    _pad_sel(pad_r, pad_c);
                    if (m7) { flip(); }
                } else if (pad_c == 1) {
                    if (pos7 < max - 1) {
                        poke(b + pos7, 32);
                        pos7 = pos7 + 1;
                        poke(b + pos7, 0);
                        _pad_field(b, pos7, x, fw7, m7);
                    }
                } else if (pad_c == 2) {
                    if (pos7 > 0) {
                        pos7 = pos7 - 1;
                        poke(b + pos7, 0);
                        _pad_field(b, pos7, x, fw7, m7);
                    }
                } else {
                    return pos7;
                }
            }
        }
        if (k7 == K_UP) {
            _pad_sel(pad_r, pad_c);
            if (pad_r == 0) {
                pad_r = 4;
                if (pad_c >= 7) { pad_c = 3; }
                else if (pad_c >= 3) { pad_c = 2; }
                else if (pad_c >= 1) { pad_c = 1; }
                else { pad_c = 0; }
            } else if (pad_r == 4) {
                pad_r = 3;
                if (pad_c == 0) { pad_c = 0; }
                else if (pad_c == 1) { pad_c = 4; }
                else if (pad_c == 2) { pad_c = 7; }
                else { pad_c = 9; }
            } else {
                pad_r = pad_r - 1;
            }
            _pad_sel(pad_r, pad_c);
            if (m7) { flip(); }
        }
        if (k7 == K_DOWN) {
            _pad_sel(pad_r, pad_c);
            if (pad_r == 3) {
                pad_r = 4;
                if (pad_c >= 7) { pad_c = 3; }
                else if (pad_c >= 4) { pad_c = 2; }
                else if (pad_c >= 1) { pad_c = 1; }
                else { pad_c = 0; }
            } else if (pad_r == 4) {
                pad_r = 0;
                if (pad_c == 0) { pad_c = 0; }
                else if (pad_c == 1) { pad_c = 4; }
                else if (pad_c == 2) { pad_c = 7; }
                else { pad_c = 9; }
            } else {
                pad_r = pad_r + 1;
            }
            _pad_sel(pad_r, pad_c);
            if (m7) { flip(); }
        }
        if (k7 == K_LEFT) {
            _pad_sel(pad_r, pad_c);
            if (pad_r < 4) { pad_c = (pad_c + 9) % 10; }
            else { pad_c = (pad_c + 3) % 4; }
            _pad_sel(pad_r, pad_c);
            if (m7) { flip(); }
        }
        if (k7 == K_RIGHT) {
            _pad_sel(pad_r, pad_c);
            if (pad_r < 4) { pad_c = (pad_c + 1) % 10; }
            else { pad_c = (pad_c + 1) % 4; }
            _pad_sel(pad_r, pad_c);
            if (m7) { flip(); }
        }
    }
}
)CWIZ";

#endif // WIZ_PRELUDE_INPUT_HPP
