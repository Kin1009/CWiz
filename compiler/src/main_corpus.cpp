// main_corpus.cpp - compile the firmware-decompiler CWiz corpus in-process.
//
// Usage: corpus_check [--named] [--out DIR] [corpus-dir]
// Reports go under <repo>/temp/corpus by default; nothing is written to /tmp.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "build.hpp"
#include "common.hpp"

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    fs::path corpus = fs::path("firmware-re/fx580/cwiz/auto");
    fs::path out = fs::path("temp/corpus");
    bool named = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--named") named = true;
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else corpus = a;
    }
    std::error_code ec;
    fs::create_directories(out, ec);
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(corpus, ec)) {
        if (!e.is_regular_file()) continue;
        std::string n = e.path().filename().string();
        if (e.path().extension() != ".cwiz" || n.rfind("_", 0) == 0) continue;
        files.push_back(e.path());
    }
    if (named) {
        fs::path nd = corpus.parent_path() / "named";
        for (const auto& e : fs::directory_iterator(nd, ec))
            if (e.is_regular_file() && e.path().extension() == ".cwiz") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    std::vector<std::string> failures;
    auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < files.size(); i++) {
        try {
            wiz::build_path(files[i].string(), "cwii", "");
        } catch (const std::exception& e) {
            failures.push_back(files[i].string() + ": " + e.what());
        }
        if ((i + 1) % 400 == 0)
            std::printf("  %zu/%zu\n", i + 1, files.size());
    }
    auto secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::ofstream rep(out / "report.txt", std::ios::trunc);
    rep << "corpus: " << corpus.string() << "\nmodules: " << files.size()
        << "\npassed: " << (files.size() - failures.size()) << "\nfailed: " << failures.size()
        << "\nseconds: " << secs << "\n";
    for (const std::string& f : failures) rep << "\n=== " << f << "\n";
    std::printf("%zu/%zu passed in %.2fs -> %s\n", files.size() - failures.size(), files.size(),
                secs, (out / "report.txt").string().c_str());
    for (size_t i = 0; i < failures.size() && i < 30; i++)
        std::printf("FAIL %s\n", failures[i].c_str());
    return failures.empty() ? 0 : 1;
}
