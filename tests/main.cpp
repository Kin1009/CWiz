// main.cpp - runs every ported suite and reports the totals.
#include <cstdlib>
#include <exception>
#include <filesystem>

#include "cli.hpp"
#include "diag.hpp"
#include "harness.hpp"

namespace {

void run_guarded(const char* name, void (*fn)()) {
    try {
        fn();
    } catch (const std::exception& e) {
        wt::check(std::string(name) + " (suite raised)", false, e.what());
    }
}

}  // namespace

int main(int argc, char** argv) {
    namespace fs = std::filesystem;

    std::string root;
    if (const char* r = std::getenv("WIZ_ROOT"))
        root = r;
    else {
        std::error_code ec;
        fs::path exe = fs::weakly_canonical(fs::path(argv[0]), ec);
        if (!ec) root = exe.parent_path().parent_path().string();
    }
    if (!root.empty()) setenv("WIZ_ROOT", root.c_str(), 1);
    wt::g_root() = root;

    wiz::g_argv0 = argv[0];
    wiz::set_data_dir(wiz::autodetect_data_dir(argv[0]));

    {
        std::error_code ec;
        fs::path exe = fs::weakly_canonical(fs::path(argv[0]), ec);
        fs::path dir = ec ? fs::absolute(fs::path(argv[0])).parent_path() : exe.parent_path();
        wt::g_wizc() = (dir / "wizc").string();
    }

    run_guarded("test_cw", wt::run_cw);
    run_guarded("test_v02", wt::run_v02);
    run_guarded("test_v03", wt::run_v03);
    run_guarded("test_v03b", wt::run_v03b);
    run_guarded("test_cwapp", wt::run_cwapp);
    run_guarded("test_cwiz", wt::run_cwiz);
    run_guarded("test_cpre", wt::run_cpre);
    run_guarded("test_lint", wt::run_lint);
    run_guarded("test_fuzz", wt::run_fuzz);

    std::printf("\n%d checks, %d failure(s)\n", wt::checks(), (int)wt::fails().size());
    if (!wt::fails().empty()) {
        std::printf("FAILED:");
        for (auto& f : wt::fails()) std::printf(" %s;", f.c_str());
        std::printf("\n");
    }
    return wt::fails().empty() ? 0 : 1;
}
