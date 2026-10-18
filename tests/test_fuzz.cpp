// test_fuzz.cpp - random-expression fuzzer: the compiler must agree with an
// independent 16-bit evaluator (ported from tests/fuzz.py).
#include <cmath>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "harness.hpp"

using namespace wt;

namespace {

int w16(int v) {
    v &= 0xFFFF;
    return (v & 0x8000) ? v - 65536 : v;
}

void tdiv(int a, int b, int& q, int& r) {
    if (b == 0) {
        q = 0;
        r = 0;
        return;
    }
    q = std::abs(a) / std::abs(b);
    if ((a < 0) != (b < 0)) q = -q;
    r = a - q * b;
}

struct Node {
    char k = 'n';
    std::string name;
    std::string op;
    int num = 0;
    std::shared_ptr<Node> a, b;
};

using NP = std::shared_ptr<Node>;

NP gen(int depth, const std::vector<std::string>& names, std::mt19937& rng) {
    std::uniform_real_distribution<double> U(0.0, 1.0);
    auto pick = [&](const std::vector<std::string>& v) {
        std::uniform_int_distribution<size_t> d(0, v.size() - 1);
        return v[d(rng)];
    };
    auto pickn = [&](const std::vector<int>& v) {
        std::uniform_int_distribution<size_t> d(0, v.size() - 1);
        return v[d(rng)];
    };
    if (depth == 0 || U(rng) < 0.2) {
        if (U(rng) < 0.5) {
            auto n = std::make_shared<Node>();
            n->k = 'v';
            n->name = pick(names);
            return n;
        }
        static const std::vector<int> nums = {0,   1,   2,     3,    5,    7,     10,    100,   255, 256,
                                              1000, 12345, 32767, -1, -2, -7, -100, -32768, -255};
        auto n = std::make_shared<Node>();
        n->k = 'n';
        n->num = pickn(nums);
        return n;
    }
    double r = U(rng);
    if (r < 0.55) {
        static const std::vector<std::string> ops = {"+", "-", "*", "/", "%", "&", "|", "^"};
        auto n = std::make_shared<Node>();
        n->k = 'b';
        n->op = pick(ops);
        n->a = gen(depth - 1, names, rng);
        n->b = gen(depth - 1, names, rng);
        return n;
    }
    if (r < 0.65) {
        static const std::vector<std::string> ops = {"<<", ">>"};
        std::uniform_int_distribution<int> d(0, 15);
        auto n = std::make_shared<Node>();
        n->k = 'b';
        n->op = pick(ops);
        n->a = gen(depth - 1, names, rng);
        n->b = std::make_shared<Node>();
        n->b->k = 'n';
        n->b->num = d(rng);
        return n;
    }
    if (r < 0.8) {
        static const std::vector<std::string> ops = {"==", "!=", "<", ">", "<=", ">="};
        auto n = std::make_shared<Node>();
        n->k = 'c';
        n->op = pick(ops);
        n->a = gen(depth - 1, names, rng);
        n->b = gen(depth - 1, names, rng);
        return n;
    }
    if (r < 0.9) {
        static const std::vector<std::string> ops = {"and", "or"};
        auto n = std::make_shared<Node>();
        n->op = pick(ops);
        n->k = n->op == "and" ? 'a' : 'o';
        n->a = gen(depth - 1, names, rng);
        n->b = gen(depth - 1, names, rng);
        return n;
    }
    if (r < 0.95) {
        auto n = std::make_shared<Node>();
        n->k = 't';
        n->a = gen(depth - 1, names, rng);
        return n;
    }
    static const std::vector<std::string> ops = {"neg", "inv"};
    auto n = std::make_shared<Node>();
    n->op = pick(ops);
    n->k = n->op == "neg" ? 'g' : 'i';
    n->a = gen(depth - 1, names, rng);
    return n;
}

std::string src_of(const NP& e) {
    switch (e->k) {
        case 'v': return e->name;
        case 'n': return "(" + std::to_string(e->num) + ")";
        case 'b':
        case 'c': return "(" + src_of(e->a) + " " + e->op + " " + src_of(e->b) + ")";
        case 'a':
        case 'o': return "(" + src_of(e->a) + " " + e->op + " " + src_of(e->b) + ")";
        case 't': return "(not " + src_of(e->a) + ")";
        case 'g': return "(-" + src_of(e->a) + ")";
        case 'i': return "(~" + src_of(e->a) + ")";
    }
    return "0";
}

int ev(const NP& e, const std::map<std::string, int>& env) {
    switch (e->k) {
        case 'v': return env.at(e->name);
        case 'n': return e->num;
        case 'b': {
            int a = ev(e->a, env), b = ev(e->b, env);
            const std::string& op = e->op;
            if (op == "+") return w16(a + b);
            if (op == "-") return w16(a - b);
            if (op == "*") return w16(a * b);
            if (op == "/") { int q, r; tdiv(a, b, q, r); return w16(q); }
            if (op == "%") { int q, r; tdiv(a, b, q, r); return w16(r); }
            if (op == "&") return w16(a & b);
            if (op == "|") return w16(a | b);
            if (op == "^") return w16(a ^ b);
            if (op == "<<") return w16((int)(((unsigned)(a & 0xFFFF)) << b));
            if (op == ">>") return w16(a >> b);
            return 0;
        }
        case 'c': {
            int a = ev(e->a, env), b = ev(e->b, env);
            const std::string& op = e->op;
            bool r = op == "==" ? a == b
                     : op == "!=" ? a != b
                     : op == "<" ? a < b
                     : op == ">" ? a > b
                     : op == "<=" ? a <= b
                                  : a >= b;
            return r ? 1 : 0;
        }
        case 'a': return (ev(e->a, env) != 0 && ev(e->b, env) != 0) ? 1 : 0;
        case 'o': return (ev(e->a, env) != 0 || ev(e->b, env) != 0) ? 1 : 0;
        case 't': return ev(e->a, env) == 0 ? 1 : 0;
        case 'g': return w16(-ev(e->a, env));
        case 'i': return w16(~ev(e->a, env));
    }
    return 0;
}

}  // namespace

void wt::run_fuzz() {
    std::mt19937 rng(7);
    int total = 0, bad = 0;
    int nprog = 12;
    int printed = 0;
    for (int prog = 0; prog < nprog; prog++) {
        std::map<std::string, int> env;
        std::uniform_int_distribution<size_t> pickv(0, 11);
        static const std::vector<int> pool = {0,    1,      -1,     5,     -5,     300,
                                              -300, 32767,  -32768, 12345, -12345, 77};
        const char* vn = "abcd";
        for (int i = 0; i < 4; i++) env[std::string(1, vn[i])] = pool[pickv(rng)];

        std::vector<std::string> names;
        for (auto& kv : env) names.push_back(kv.first);

        std::uniform_int_distribution<int> pickd(1, 4);
        std::vector<NP> exprs;
        for (int i = 0; i < 25; i++) exprs.push_back(gen(pickd(rng), names, rng));

        std::string code;
        for (auto& kv : env) code += kv.first + " = " + std::to_string(kv.second) + "\n";
        for (size_t i = 0; i < exprs.size(); i++)
            code += "r" + std::to_string(i) + " = " + src_of(exprs[i]) + "\n";

        wiz::BuildResult br = wiz::build_source(code, "");
        wiz::U8 m(br.rom);
        std::string res = m.run(5000000);
        if (res.find("B to self") == std::string::npos) {
            bad++;
            if (printed++ < 6) std::printf("MISMATCH (no halt) program %d: %s\n", prog, res.c_str());
            continue;
        }
        for (size_t i = 0; i < exprs.size(); i++) {
            int a = br.variables.at("r" + std::to_string(i));
            int got = w16(m.mem[a] | (m.mem[a + 1] << 8));
            int exp = ev(exprs[i], env);
            total++;
            if (got != exp) {
                bad++;
                if (printed++ < 6) {
                    std::printf("MISMATCH %s env", src_of(exprs[i]).c_str());
                    for (auto& kv : env) std::printf(" %s=%d", kv.first.c_str(), kv.second);
                    std::printf(" got %d exp %d\n", got, exp);
                }
            }
        }
    }
    std::printf("expressions %d mismatches %d\n", total, bad);
    check("fuzz: random expressions match the interpreter", bad == 0,
          ex(total, bad));
}
