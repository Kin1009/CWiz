// test_prelude_ports.cpp - the upstream Wiz PRELUDE surface ported into CWiz.
//
// Source taken from the authoritative upstream wizc.py PRELUDE at
// pkdev-en/Wiz@3a4308a: shapes/clip, scaled text, hex bitmaps, rotated
// sprites, interactive input, the word/heap/RAM-file helpers and the RTC /
// elapsed-time helpers.  Input helpers block on keys and rtc_alive() waits
// for a ticking RTC, so they are verified at compile/link time only; every
// other helper is exercised deterministically in the simulator.
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

#include "harness.hpp"

using namespace wt;

namespace {

// A superset of the upstream-sync smoke program: every function and every
// state global introduced by the three new prelude headers is referenced.
// Kept in-repo (rather than read from temp/) so the test has no external
// dependency.  Never simulated: input_num/input_tap/input_pad/waitkey/getc
// all wait for keys.
const char* const kSmoke = R"CWIZ(
int g = 0;
int r[8];
void main() {
    clip(2, 2, 100, 40);
    g = _clip_x0 + _clip_y1;
    ellipse(50, 20, 10, 8);
    fillellipse(50, 20, 10, 8);
    tri(0, 0, 10, 0, 5, 10);
    filltri(0, 0, 10, 0, 5, 10);
    roundrect(2, 2, 20, 10, 3);
    charz(0, 0, 65, 8, 6);
    g = textz(0, 10, 8, 6, "Hi");
    g = numz(0, 20, 8, 6, 123);
    g = _hexv(65);
    draw(0, 30, 8, 8, "3C7EFF");
    sprite_rot(10, 30, 8, 8, r, 1, 1, 2);
    _sprite_px(10, 30, 8, 8, r, 2, 1, 0);
    waitkey(1);
    g = input_num(0, 40, 0);
    g = input_tap(r, 8, 0, 50);
    g = input_pad(r, 8, 0, 50);
    g = getc();
    g = getc_tap();
    g = _tap_char(2, 1);
    g = _dig(5);
    g = _wkl;
    g = _pad_ch(0, 0);
    g = _pdig(5);
    _pad_draw_grid();
    _pad_sel(0, 0);
    _pad_redraw_letters();
    _pad_field(r, 0, 0, 8, 0);
    g = alloc(16);
    _merge();
    free(g);
    g = _rdw(0xE818);
    _wrw(0xE818, g);
    g = f_save("t1", r, 8);
    g = f_load("t1", r, 8);
    g = f_exists("t1");
    f_remove("t1");
    g = _dec(0x59) + _bcd(12) + _weekday(2026, 11, 24);
    g = hour();
    g = minute();
    g = second();
    g = day();
    g = weekday();
    g = month();
    g = year();
    g = _rtc[0];
    rtc_read();
    g = rtc_alive();
    rtc_enable();
    rtc_disable();
    rtc_reset();
    set_time(1, 2, 3);
    set_date(2026, 11, 24);
    set_hours(1);
    set_minutes(2);
    set_seconds(3);
    set_day(24);
    set_month(11);
    set_year(2026);
    draw_time(0, 55);
    draw_date(0, 56);
    g = elapsed(ticks());
}
)CWIZ";

// Every new top-level declaration.  The assembler prefixes function labels
// with "f_"; globals live in BuildResult::variables instead.
const char* const kFuncs[] = {
    "ellipse", "fillellipse", "tri", "filltri", "roundrect", "clip", "charz", "textz",
    "numz", "_hexv", "draw", "sprite_rot", "_sprite_px", "waitkey", "input_num",
    "input_tap", "input_pad", "getc", "getc_tap", "_tap_char", "_dig", "_pad_ch",
    "_pad_draw_grid", "_pad_sel", "_pad_redraw_letters", "_pad_field", "_pdig",
    "_rdw", "_wrw", "alloc", "_merge", "free", "f_save", "f_load", "f_exists",
    "f_remove", "_dec", "_bcd", "_weekday", "hour", "minute", "second", "day",
    "weekday", "month", "year", "rtc_read", "rtc_alive", "rtc_enable", "rtc_disable",
    "rtc_reset", "set_time", "set_date", "set_hours", "set_minutes", "set_seconds",
    "set_day", "set_month", "set_year", "draw_time", "draw_date", "elapsed",
};
const char* const kGlobals[] = {
    "_clip_x0", "_clip_x1", "_clip_y0", "_clip_y1", "_wkl", "_gc_pend",
    "_pad_up", "_pad_cs", "_rtc",
};

Machine run(const std::string& src, const std::string& target = "cwii", long steps = 4000000) {
    wiz::BuildResult br = wiz::build_cwiz(src, target);
    wiz::U8 m(br.rom, {}, target);
    std::string why = m.run(steps);
    return Machine{std::move(m), br.variables, why, br.rom, br.full_asm, br.syms};
}

int R(const Machine& m, const char* n) { return var_of(m.m, m.vars, n); }

std::vector<int> vals(const Machine& m, std::initializer_list<const char*> names) {
    std::vector<int> out;
    for (auto* n : names) out.push_back(var_of(m.m, m.vars, n));
    return out;
}

std::string build_error(const std::string& src, const std::string& target) {
    try {
        wiz::build_cwiz(src, target);
    } catch (const std::exception& e) {
        return e.what();
    }
    return "";
}

bool clean_run(const Machine& m) {
    return m.why.find("max steps") == std::string::npos &&
           m.why.find("fault") == std::string::npos;
}

}  // namespace

void wt::run_prelude_ports() {
    // ------------------------------------------------- compile / link surface
    {
        const char* targets[] = {"580", "880", "cwapp"};
        for (auto* t : targets) {
            std::string err = build_error(kSmoke, t);
            check(std::string("prelude ports: full smoke source compiles for target ") + t,
                  err.empty(), err);
        }
    }
    {
        std::string err;
        wiz::BuildResult br;
        try {
            br = wiz::build_cwiz(kSmoke, "580");
        } catch (const std::exception& e) {
            err = e.what();
        }
        std::vector<std::string> missing;
        for (auto* n : kFuncs)
            if (!br.syms.count(std::string("f_") + n)) missing.push_back(n);
        for (auto* n : kGlobals)
            if (!br.variables.count(n)) missing.push_back(n);
        check("prelude ports: every new function and state global is linked",
              err.empty() && br.rom.size() > 1000 && missing.empty(),
              ex(err, ss(missing)));
    }

    // --------------------------------------------------------- clip window
    {
        Machine m = run("int R; void main() {"
                        " clear(); hline(0,20,10); vline(5,0,20);"
                        " clip(2,3,4,5);"
                        " clear(); hline(0,20,10); vline(5,0,20); hline(0,20,4); vline(1,0,20);"
                        " render();"
                        " R = getpixel(5,4) + getpixel(2,4)*10 + getpixel(5,7)*100"
                        "   + getpixel(1,4)*1000 + getpixel(5,10)*10000; }");
        check("clip() restricts hline()/vline() to the window", R(m, "R") == 111,
              ex(R(m, "R"), m.why));
        check("clip(): only eight pixels survive the window", lit(m.m).size() == 8,
              ex((int)lit(m.m).size(), m.why));
    }

    // ------------------------------------------------------------- ellipse
    {
        Machine m = run("int R; void main() { clear(); ellipse(40,30,8,6); render();"
                        " R = getpixel(48,30) + getpixel(32,30)*10 + getpixel(40,36)*100"
                        "   + getpixel(40,24)*1000 + getpixel(40,30)*10000; }");
        check("ellipse() draws the four extremes, not its interior", R(m, "R") == 1111,
              ex(R(m, "R"), m.why));
    }
    {
        Machine m = run("int R; void main() { clear(); fillellipse(40,30,8,6); render();"
                        " R = getpixel(40,30) + getpixel(40,24)*10 + getpixel(31,30)*100"
                        "   + getpixel(49,30)*1000; }");
        check("fillellipse() fills the centre and edge rows", R(m, "R") == 111,
              ex(R(m, "R"), m.why));
        size_t n = lit(m.m).size();
        check("fillellipse() covers a plausible area", n > 100 && n < 250, ss((int)n));
    }

    // ---------------------------------------------------------- triangles
    {
        Machine m = run("int R; void main() { clear(); tri(2,2,20,2,11,18); render();"
                        " R = getpixel(2,2) + getpixel(20,2)*10 + getpixel(11,18)*100"
                        "   + getpixel(11,2)*1000 + getpixel(2,18)*10000; }");
        check("tri() connects all three vertices", R(m, "R") == 1111, ex(R(m, "R"), m.why));
    }
    {
        Machine m = run("int R; void main() { clear(); filltri(2,2,20,2,11,18); render();"
                        " R = getpixel(2,2) + getpixel(11,10)*10 + getpixel(11,17)*100"
                        "   + getpixel(2,18)*1000 + getpixel(20,18)*10000; }");
        check("filltri() fills the interior but not outside", R(m, "R") == 111,
              ex(R(m, "R"), m.why));
        size_t n = lit(m.m).size();
        check("filltri() covers a plausible area", n > 100 && n < 300, ss((int)n));
    }

    // ---------------------------------------------------------- roundrect
    {
        Machine m = run("int R; void main() { clear(); roundrect(2,2,20,10,3); render();"
                        " R = getpixel(5,2) + getpixel(18,2)*10 + getpixel(2,5)*100"
                        "   + getpixel(21,5)*1000 + getpixel(5,11)*10000; }");
        check("roundrect() draws both spans and rounded corners", R(m, "R") == 11111,
              ex(R(m, "R"), m.why));
    }

    // ------------------------------------------------- hex bitmap / sprite
    {
        Machine m = run("int V,R; void main() { clear(); V = _hexv(65)*10 + _hexv(57);"
                        " draw(10,10,4,8,\"80010108\"); render();"
                        " R = getpixel(10,10) + getpixel(17,11)*10"
                        "   + getpixel(17,12)*100 + getpixel(14,13)*1000; }");
        check("_hexv() decodes upper- and lower-case hex digits", R(m, "V") == 109,
              ex(R(m, "V"), m.why));
        check("draw() expands a ROM8 hex string into pixels", R(m, "R") == 1111,
              ex(R(m, "R"), m.why));
    }
    {
        Machine m = run("rom8 char spr[8] = {0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01};"
                        "int R; void main() { clear(); sprite_rot(10,10,8,8,spr,1,1,0);"
                        " sprite_rot(10,30,8,8,spr,1,1,1); render();"
                        " R = getpixel(10,10) + getpixel(17,17)*10"
                        "   + getpixel(17,30)*100 + getpixel(10,37)*1000; }");
        check("sprite_rot() draws unrotated and 90-degree sprites", R(m, "R") == 1111,
              ex(R(m, "R"), m.why));
    }

    // -------------------------------------------------------- scaled text
    {
        Machine m = run("int R; void main() { clear(); charz(0,0,65,8,6);"
                        " R = textz(0,10,8,6,\"Hi\") + numz(0,20,8,6,123)*100"
                        "   + getpixel(0,10)*10000; render(); }");
        check("charz()/textz()/numz() exist and advance by the scaled width",
              R(m, "R") == 11812, ex(R(m, "R"), m.why));
        size_t n = lit(m.m).size();
        check("scaled glyphs actually draw pixels", n > 20 && clean_run(m),
              ex((int)n, m.why));
    }

    // --------------------------------------------------------------- timer
    {
        Machine m = run("int R; void main() { int t = ticks(); R = elapsed(t); }");
        check("elapsed(ticks()) is near zero in the deterministic simulator",
              R(m, "R") >= 0 && R(m, "R") < 1000, ex(R(m, "R"), m.why));
    }

    // -------------------------------------------------- heap / word helpers
    {
        int pk = wiz::get_target("cwii").var_base + 0xF00;
        std::string src = wiz::replace_all(
            "int H0,H1,V,W0,W1; void main() { int p = alloc(16);"
            " H0 = _rdw(p-2); poke(p,0x5A); V = peek(p); free(p); H1 = _rdw(p-2);"
            " _wrw(@PK,0x1234); W0 = _rdw(@PK); W1 = peek(@PK) + peek(@PK+1)*256; }",
            "@PK", wiz::fmt("0x%X", pk));
        Machine m = run(src);
        check("alloc() returns a buffer with a valid 16-bit header",
              R(m, "H0") == 18 && R(m, "V") == 90, ex(vals(m, {"H0", "V"}), m.why));
        int h1 = uvar_of(m.m, m.vars, "H1");
        check("free() marks the block free and merges it back",
              (h1 & 0x8000) != 0 && (h1 & 0x7FFF) >= 18, ex(h1));
        check("_wrw()/_rdw() round-trip a little-endian word",
              R(m, "W0") == 0x1234 && R(m, "W1") == 0x1234,
              ex(vals(m, {"W0", "W1"})));
    }

    // ------------------------------------------------------- RAM file system
    {
        Machine m = run("char buf[8]; char out[8];"
                        "int A,B,C,D,E,F,G,H,I,J; void main() {"
                        " buf[0]=65; buf[1]=66; buf[2]=67;"
                        " A=f_exists(\"t1\"); B=f_save(\"t1\",buf,3); C=f_exists(\"t1\");"
                        " D=f_load(\"t1\",out,8); E=out[0]; F=out[1]; G=out[2];"
                        " H=f_remove(\"t1\"); I=f_exists(\"t1\"); J=f_load(\"t1\",out,8); }");
        std::vector<int> want = {0, 1, 1, 3, 65, 66, 67, 1, 0, -1};
        check("f_save()/f_load()/f_exists()/f_remove() round-trip RAM files",
              vals(m, {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"}) == want,
              ex(vals(m, {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"}), m.why));
    }

    // -------------------------------------------------- BCD / date helpers
    {
        Machine m = run("int A,B,C; void main() { A=_dec(0x59); B=_bcd(42);"
                        " C=_weekday(2024,2,29); }");
        check("_dec()/_bcd()/_weekday() compute deterministic values",
              vals(m, {"A", "B", "C"}) == std::vector<int>({59, 66, 4}),
              ex(vals(m, {"A", "B", "C"}), m.why));
    }

    // ------------------------------------------------------------ RTC read
    {
        Machine m = run("int A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P; void main() {"
                        " poke(0xF0C0,0x59); poke(0xF0C1,0x58); poke(0xF0C2,0x23);"
                        " poke(0xF0C3,24); poke(0xF0C4,2); poke(0xF0C5,11); poke(0xF0C6,0);"
                        " poke(0xF0C7,1);"
                        " A=second()+minute()*100; B=hour()+day()*100;"
                        " C=weekday()+month()*100; D=year(); rtc_read();"
                        " E=_rtc[0]; F=_rtc[1]; G=_rtc[2]; H=_rtc[3]; I=_rtc[4];"
                        " J=_rtc[5]; K=_rtc[6];"
                        " L=peek(0xF0C7); M=peek(0xF0C0); N=peek(0xF0C2);"
                        " O=peek(0xF0C3); P=peek(0xF0C4); }");
        std::vector<int> want = {5859, 2423, 1102, 2026, 23, 58, 59, 24, 2, 11, 2026,
                                 1, 0x59, 0x23, 24, 2};
        const char* names[] = {"A", "B", "C", "D", "E", "F", "G", "H",
                               "I", "J", "K", "L", "M", "N", "O", "P"};
        std::vector<int> got;
        for (auto* n : names) got.push_back(R(m, n));
        check("RTC getters and rtc_read() decode scripted registers", got == want,
              ex(got, m.why));
    }

    // ------------------------------------------------------------ RTC set
    {
        Machine m = run("int A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P; void main() {"
                        " set_time(1,2,3);"
                        " A=peek(0xF0C0); B=peek(0xF0C1); C=peek(0xF0C2);"
                        " set_date(2027,3,4);"
                        " D=peek(0xF0C6); E=peek(0xF0C5); F=peek(0xF0C3); G=peek(0xF0C4);"
                        " set_hours(6); set_minutes(7); set_seconds(8);"
                        " set_day(9); set_month(5); set_year(2028);"
                        " H=hour()+minute()*100+second()*1000; I=day(); J=month(); K=year();"
                        " rtc_enable(); L=peek(0xF0C7); rtc_disable(); M=peek(0xF0C7);"
                        " rtc_reset(); N=peek(0xF0C0); O=peek(0xF0C3); P=peek(0xF0C7); }");
        std::vector<int> want = {3, 2, 1, 1, 3, 4, 4, 8706, 9, 5, 2028, 1, 0, 0, 1, 0};
        check("RTC setters, enable/disable and reset poke the right registers",
              vals(m, {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K",
                       "L", "M", "N", "O", "P"}) == want,
              ex(vals(m, {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K",
                          "L", "M", "N", "O", "P"}), m.why));
    }

    // ------------------------------------------------------- RTC display
    {
        Machine m = run("void main() { set_time(12,34,56); set_date(2026,11,24);"
                        " draw_time(0,20); draw_date(0,40); render(); }");
        size_t n = lit(m.m).size();
        check("draw_time()/draw_date() render without crashing", clean_run(m) && n > 20,
              ex((int)n, m.why));
    }
}
