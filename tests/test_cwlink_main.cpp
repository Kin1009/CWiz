// test_cwlink_main.cpp - standalone CTest entry for the .cwapplink fixture.
#include <cstdlib>
#include <filesystem>
#include <string>

#include "cli.hpp"
#include "harness.hpp"

int main(int argc, char** argv) {
    (void)argc;
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
    wiz::set_data_dir(wiz::autodetect_data_dir(argv[0]));
    {
        std::error_code ec;
        fs::path exe = fs::weakly_canonical(fs::path(argv[0]), ec);
        fs::path dir = ec ? fs::absolute(fs::path(argv[0])).parent_path() : exe.parent_path();
        wt::g_wizc() = (dir / "wizc").string();
    }
    wt::run_cwlink();
    std::printf("\n%d checks, %d failure(s)\n", wt::checks(), (int)wt::fails().size());
    if (!wt::fails().empty()) {
        std::printf("FAILED:");
        for (const std::string& f : wt::fails()) std::printf(" %s", f.c_str());
        std::printf("\n");
        return 1;
    }
    return 0;
}
