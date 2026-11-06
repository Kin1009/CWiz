// test_cwmode_main.cpp - standalone CTest entry for the cwmode regression
// fixture.  The same cases also run inside `wiztests`; this binary exists so
// CTest can report the ROM patcher separately and keep the fixture visible.
#include <cstdlib>
#include <filesystem>
#include <string>

#include "harness.hpp"

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
    if (!root.empty()) {
        setenv("WIZ_ROOT", root.c_str(), 1);
        wt::g_root() = root;
    }
    wt::run_cwmode();
    std::printf("\n%d checks, %d failure(s)\n", wt::checks(), (int)wt::fails().size());
    if (!wt::fails().empty()) {
        std::printf("FAILED:");
        for (const std::string& f : wt::fails()) std::printf(" %s", f.c_str());
        std::printf("\n");
        return 1;
    }
    return 0;
}
