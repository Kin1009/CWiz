// harness.hpp - shared helpers for the ported Wiz/CWiz test suite.
//
// The Python suite printed "PASS <name>" / "FAIL <name>" for every check and
// accumulated failures in a global list.  This header reproduces that exactly:
// identical check names, identical pass/fail semantics, extra text only on
// failure.
#pragma once

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <sys/wait.h>

#include "asm.hpp"
#include "build.hpp"
#include "cwiz.hpp"
#include "sim.hpp"
#include "wiz.hpp"

namespace wt {

void run_rop();

// ---------------------------------------------------------------- reporting
inline std::vector<std::string>& fails() {
    static std::vector<std::string> f;
    return f;
}
inline int& checks() {
    static int c = 0;
    return c;
}

inline void check(const std::string& name, bool cond, const std::string& extra = "") {
    checks()++;
    if (cond) {
        std::printf("PASS %s\n", name.c_str());
    } else {
        std::printf("FAIL %s", name.c_str());
        if (!extra.empty()) std::printf("  %s", extra.c_str());
        std::printf("\n");
        fails().push_back(name);
    }
}

// ---------------------------------------------------------------- small bits
inline int s16(int v) {
    v &= 0xFFFF;
    return (v & 0x8000) ? v - 65536 : v;
}

// ---- repr helpers, used only to build "extra" diagnostics ----------------
inline std::string ss(const std::string& s) { return s; }
inline std::string ss(const char* s) { return s; }
inline std::string ss(bool b) { return b ? "True" : "False"; }
inline std::string ss(int v) { return std::to_string(v); }
inline std::string ss(unsigned v) { return std::to_string(v); }
inline std::string ss(long v) { return std::to_string(v); }
inline std::string ss(long long v) { return std::to_string(v); }
inline std::string ss(const std::pair<int, int>& p) {
    return "(" + std::to_string(p.first) + ", " + std::to_string(p.second) + ")";
}
template <class T>
inline std::string ss(const std::vector<T>& v) {
    std::string r = "[";
    for (size_t i = 0; i < v.size(); i++) {
        if (i) r += ", ";
        r += ss(v[i]);
    }
    return r + "]";
}
template <class T>
inline std::string ss(const std::set<T>& s) {
    std::string r = "{";
    size_t i = 0;
    for (const auto& e : s) {
        if (i++) r += ", ";
        if (i > 8) { r += "..."; break; }
        r += ss(e);
    }
    return r + "}";
}
inline std::string ex() { return ""; }
template <class A, class... R>
inline std::string ex(const A& a, const R&... rest) {
    std::string t = ex(rest...);
    return t.empty() ? ss(a) : ss(a) + ", " + t;
}

// ---------------------------------------------------------------- machine bits
inline std::set<std::pair<int, int>> lit(const wiz::U8& m) {
    std::set<std::pair<int, int>> out;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 192; x++)
            if (m.pixel(x, y)) out.insert({x, y});
    return out;
}

inline std::set<std::pair<int, int>> fb(const wiz::U8& m) {
    std::set<std::pair<int, int>> out;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 192; x++)
            if (m.mem[0xE000 + y * 32 + (x >> 3)] & (0x80 >> (x & 7))) out.insert({x, y});
    return out;
}

inline int var_of(const wiz::U8& m, const std::map<std::string, int>& v, const std::string& name) {
    int a = v.at(name);
    return s16(m.mem[a] | (m.mem[a + 1] << 8));
}

inline int uvar_of(const wiz::U8& m, const std::map<std::string, int>& v, const std::string& name) {
    int a = v.at(name);
    return m.mem[a] | (m.mem[a + 1] << 8);
}

inline std::pair<int, int> key_pair(int k) { return {0x80 >> (k >> 3), 0x80 >> (k & 7)}; }

// The Wiz key table (cwii / default) - Python's `KEY_CONSTS`.
inline const std::map<std::string, int>& key_consts_default() {
    return wiz::key_consts(wiz::DEFAULT_TARGET);
}

struct Machine {
    wiz::U8 m;
    std::map<std::string, int> vars;
    std::string why;
    std::vector<uint8_t> rom;
    std::string full;
    std::map<std::string, int> syms;
};

inline Machine run_src(const std::string& src, const std::string& target = "",
                       std::vector<std::pair<int, int>> keys = {}, long steps = 30000000) {
    wiz::BuildResult br = wiz::build_source(src, target);
    wiz::U8 m(br.rom, keys, target);
    std::string why = m.run(steps);
    return Machine{std::move(m), br.variables, why, br.rom, br.full_asm, br.syms};
}

// ---------------------------------------------------------------- repo paths
inline std::string& g_root() {
    static std::string s;
    return s;
}
inline std::string& g_wizc() {
    static std::string s;
    return s;
}
inline std::string root_path(const std::string& rel) {
    return g_root().empty() ? rel : g_root() + "/" + rel;
}

// ---------------------------------------------------------------- CLI driver
struct CliResult {
    int rc = 0;
    std::string out;
};

inline CliResult cli_run(const std::string& workdir, const std::vector<std::string>& args) {
    std::string cmd = "cd '" + workdir + "' && '" + g_wizc() + "'";
    for (const auto& a : args) cmd += " '" + a + "'";
    cmd += " 2>&1";
    CliResult res;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) {
        res.rc = -1;
        return res;
    }
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), p)) res.out += buf;
    int st = pclose(p);
    res.rc = WIFEXITED(st) ? WEXITSTATUS(st) : st;
    return res;
}

// ---------------------------------------------------------------- suites
void run_v02();
void run_v03();
void run_v03b();
void run_cw();
void run_cwiz();
void run_cpre();
void run_cwapp();
void run_lint();
void run_fuzz();

}  // namespace wt
