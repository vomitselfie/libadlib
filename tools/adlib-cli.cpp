// adlib: inspection / authoring tools for ADLIB plans (docs/tools.md).
//
//   adlib [vocabulary options] <command> [args]
//
//   vocabulary: --symbols DIR | --enumids F ... | --enum FILE | --list NS=FILE | --hint ACF:i=NS
//   commands:   dump, decompile, graph, inspect, compare, trace
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "adlib/version.h"
#include "adlib/tools.h"

using namespace adlib;
using namespace adlib::tools;
namespace fs = std::filesystem;

namespace {

const char *kUsage = R"(usage: adlib [vocabulary] <command> [options]

vocabulary (as adlibc; any combination, loaded in order):
  --symbols DIR       a symbol directory: the 1996 layout (enumIDs.h, AnimData/Animassm.txt,
                      Sndfiles.lst, Infobtxt.lst), or *.H enum headers + *.LST lists
  --enumids F [--anims F] [--sounds F] [--texts F]   the four compiler files individually
  --enum FILE         one enum header        --list NS=FILE   one list file into namespace NS
  --hint ACF:i=NS     DO_ACTION argument i of ACF names a symbol of namespace NS (or @Role)

commands:
  dump [--tokens|--tree|--raw] FILE.PBN       raw token dump / symbolic tree (default --tree)
  decompile [-o DIR|-] [--annotate|--annotate-offsets] [--crlf] [--no-header] [--plans DIR] FILE.PBN...
                                              PBN -> ADLIB compiler source (byte-identical recompile)
  graph [--mermaid] [-o OUT] [--plans DIR] [--no-cond] [--no-timers] [--no-globals] FILE.PBN
  graph --all [--full] [--mermaid] [-o OUT] DIR
                                              behaviour-transition graph / cross-plan graph
  inspect [--plans DIR] [--lint-only] FILE.PBN...
                                              statistics, lint, unknown ids, unreachable behaviours
  compare A.PBN B.PBN                         structural diff with tree paths
  trace FILTER [LOG]                          filter a trace log (stdin by default)
  --version                                   print the version
)";

int usage() {
    std::fputs(kUsage, stderr);
    return 2;
}

std::string upperStem(const std::string &p) {
    std::string s = fs::path(p).stem().string();
    for (char &c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

bool loadPlan(const std::string &path, std::vector<uint32_t> &words, PlanTree &tree, std::string &err) {
    if (!readPbnFile(path, words, &err)) return false;
    if (!buildPlanTree(words, LoadHooks{}, tree, &err)) return false;
    tree.name = upperStem(path);
    return true;
}

std::vector<std::string> pbnFiles(const std::string &dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto &de : fs::directory_iterator(dir, ec)) {
        std::string e = de.path().extension().string();
        for (char &c : e) c = char(std::toupper(uint8_t(c)));
        if (de.is_regular_file() && e == ".PBN") out.push_back(de.path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::map<std::string, std::vector<std::string>> planBehaviours(const std::string &dir) {
    std::map<std::string, std::vector<std::string>> m;
    for (auto &f : pbnFiles(dir)) {
        std::vector<uint32_t> w;
        std::string err;
        if (readPbnFile(f, w, &err)) m[upperStem(f)] = behaviourNames(w);
    }
    return m;
}

bool writeOut(const std::string &path, const std::string &text) {
    if (path.empty() || path == "-") {
        std::fwrite(text.data(), 1, text.size(), stdout);
        return true;
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "adlib: cannot write %s\n", path.c_str());
        return false;
    }
    f << text;
    return true;
}

// ---- commands ---------------------------------------------------------------------------------

int cmdDump(SymbolTable &syms, std::vector<std::string> a) {
    std::string mode = "--tree", file;
    for (auto &x : a) {
        if (x == "--tokens" || x == "--tree" || x == "--raw") mode = x;
        else file = x;
    }
    if (file.empty()) return usage();
    std::vector<uint32_t> w;
    PlanTree t;
    std::string err;
    if (mode == "--tokens") {
        if (!readPbnFile(file, w, &err)) { std::fprintf(stderr, "adlib: %s\n", err.c_str()); return 1; }
        std::fputs(dumpTokens(w).c_str(), stdout);
        return 0;
    }
    if (!loadPlan(file, w, t, err)) { std::fprintf(stderr, "adlib: %s: %s\n", file.c_str(), err.c_str()); return 1; }
    if (mode == "--raw") {
        for (auto &n : t.top) std::fputs(dumpNode(*n).c_str(), stdout);
    } else {
        std::fputs(dumpPlan(t, syms.empty() ? nullptr : &syms.vocab()).c_str(), stdout);
    }
    return 0;
}

int cmdDecompile(SymbolTable &syms, std::vector<std::string> a) {
    DecompileOptions opt;
    std::string outDir, plansDir;
    std::vector<std::string> files;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] == "-o" && i + 1 < a.size()) outDir = a[++i];
        else if (a[i] == "--plans" && i + 1 < a.size()) plansDir = a[++i];
        else if (a[i] == "--annotate") opt.annotate = true;
        else if (a[i] == "--annotate-offsets") opt.annotate = opt.annotateOffsets = true;
        else if (a[i] == "--crlf") opt.crlf = true;
        else if (a[i] == "--no-header") opt.header = false;
        else files.push_back(a[i]);
    }
    if (files.empty()) return usage();
    if (files.size() == 1 && fs::is_directory(files[0])) files = pbnFiles(files[0]);
    std::map<std::string, std::map<std::string, std::vector<std::string>>> cache;
    int fails = 0;
    if (!outDir.empty() && outDir != "-") fs::create_directories(outDir);
    for (auto &f : files) {
        std::string dir = plansDir.empty() ? fs::path(f).parent_path().string() : plansDir;
        if (dir.empty()) dir = ".";
        if (!cache.count(dir)) cache[dir] = planBehaviours(dir);
        opt.planBehaviours = &cache[dir];
        opt.planName = upperStem(f);
        std::vector<uint32_t> w;
        std::string err, src;
        if (!readPbnFile(f, w, &err) || !decompile(w, syms, opt, src, &err)) {
            std::fprintf(stderr, "adlib: %s: %s\n", f.c_str(), err.c_str());
            ++fails;
            continue;
        }
        if (!err.empty()) std::fprintf(stderr, "adlib: %s: %s\n", f.c_str(), err.c_str());
        if (outDir.empty() || outDir == "-") writeOut("-", src);
        else if (!writeOut((fs::path(outDir) / (opt.planName + ".txt")).string(), src)) ++fails;
    }
    if (!outDir.empty() && outDir != "-")
        std::fprintf(stderr, "%zu/%zu decompiled -> %s\n", files.size() - size_t(fails), files.size(), outDir.c_str());
    return fails ? 1 : 0;
}

int cmdGraph(SymbolTable &syms, std::vector<std::string> a) {
    GraphOptions go;
    bool mermaid = false, all = false, full = false;
    std::string out, plansDir, file;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] == "--mermaid") mermaid = true;
        else if (a[i] == "--dot") mermaid = false;
        else if (a[i] == "--all") all = true;
        else if (a[i] == "--full") full = true;
        else if (a[i] == "--no-cond") go.showConditions = false;
        else if (a[i] == "--no-timers") go.showTimers = false;
        else if (a[i] == "--no-globals") go.showGlobals = false;
        else if (a[i] == "-o" && i + 1 < a.size()) out = a[++i];
        else if (a[i] == "--plans" && i + 1 < a.size()) plansDir = a[++i];
        else file = a[i];
    }
    if (file.empty()) return usage();
    const SymbolTable *sp = syms.empty() ? nullptr : &syms;
    if (all) {
        auto pb = planBehaviours(file);
        std::vector<PlanModel> models;
        for (auto &f : pbnFiles(file)) {
            std::vector<uint32_t> w;
            PlanTree t;
            std::string err;
            if (!loadPlan(f, w, t, err)) { std::fprintf(stderr, "adlib: %s: %s\n", f.c_str(), err.c_str()); continue; }
            models.push_back(analyze(t, sp, &pb));
        }
        return writeOut(out, mermaid ? graphMermaidAll(models, go) : graphDotAll(models, full, go)) ? 0 : 1;
    }
    std::string dir = plansDir.empty() ? fs::path(file).parent_path().string() : plansDir;
    auto pb = planBehaviours(dir.empty() ? "." : dir);
    std::vector<uint32_t> w;
    PlanTree t;
    std::string err;
    if (!loadPlan(file, w, t, err)) { std::fprintf(stderr, "adlib: %s: %s\n", file.c_str(), err.c_str()); return 1; }
    PlanModel m = analyze(t, sp, &pb);
    return writeOut(out, mermaid ? graphMermaid(m, go) : graphDot(m, go)) ? 0 : 1;
}

int cmdInspect(SymbolTable &syms, std::vector<std::string> a) {
    std::string plansDir;
    bool lintOnly = false;
    std::vector<std::string> files;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] == "--plans" && i + 1 < a.size()) plansDir = a[++i];
        else if (a[i] == "--lint-only") lintOnly = true;
        else files.push_back(a[i]);
    }
    if (files.empty()) return usage();
    if (files.size() == 1 && fs::is_directory(files[0])) files = pbnFiles(files[0]);
    const SymbolTable *sp = syms.empty() ? nullptr : &syms;
    int totalErr = 0;
    for (auto &f : files) {
        std::string dir = plansDir.empty() ? fs::path(f).parent_path().string() : plansDir;
        if (dir.empty()) dir = ".";
        auto pb = planBehaviours(dir);
        std::vector<uint32_t> w;
        PlanTree t;
        std::string err;
        if (!loadPlan(f, w, t, err)) { std::fprintf(stderr, "adlib: %s: %s\n", f.c_str(), err.c_str()); ++totalErr; continue; }
        PlanModel m = analyze(t, sp, &pb);
        // entries from other plans in the directory
        std::vector<int> roots;
        for (auto &g : pbnFiles(dir)) {
            if (upperStem(g) == t.name) continue;
            std::vector<uint32_t> w2;
            PlanTree t2;
            if (!loadPlan(g, w2, t2, err)) continue;
            for (auto &tr : analyze(t2, nullptr, &pb).transitions)
                if (tr.kind == Transition::Kind::Plan && upperStem(tr.plan) == t.name) roots.push_back(tr.to);
        }
        int ne = 0;
        if (lintOnly) {
            auto issues = lintPlan(t, sp, &w);
            if (sp) { auto ids = checkIds(t, *sp); issues.insert(issues.end(), ids.begin(), ids.end()); }
            for (auto &i : issues) {
                if (i.severity == Issue::Severity::Info) continue;
                ne += i.severity == Issue::Severity::Error;
                std::printf("%s: %s: %s: %s\n", t.name.c_str(), i.severity == Issue::Severity::Error ? "error" : "warning",
                            i.path.c_str(), i.message.c_str());
            }
        } else {
            std::fputs(inspectReport(t, m, sp, &w, roots, &ne).c_str(), stdout);
            if (files.size() > 1) std::printf("\n");
        }
        totalErr += ne;
    }
    return totalErr ? 1 : 0;
}

int cmdCompare(SymbolTable &syms, std::vector<std::string> a) {
    if (a.size() != 2) return usage();
    std::vector<uint32_t> wa, wb;
    PlanTree ta, tb;
    std::string err;
    if (!loadPlan(a[0], wa, ta, err)) { std::fprintf(stderr, "adlib: %s: %s\n", a[0].c_str(), err.c_str()); return 2; }
    if (!loadPlan(a[1], wb, tb, err)) { std::fprintf(stderr, "adlib: %s: %s\n", a[1].c_str(), err.c_str()); return 2; }
    std::string out;
    int n = comparePlans(ta, tb, syms.empty() ? nullptr : &syms, out);
    if (n == 0) {
        std::printf(wa == wb ? "identical (byte-identical token streams)\n"
                             : "structurally identical (token streams differ only outside the tree)\n");
        return 0;
    }
    std::fputs(out.c_str(), stdout);
    std::printf("%d difference%s\n", n, n == 1 ? "" : "s");
    return 1;
}

int cmdTrace(std::vector<std::string> a) {
    if (a.empty()) return usage();
    TraceFilter f;
    std::string err;
    if (!f.parse(a[0], &err)) { std::fprintf(stderr, "adlib: %s\n", err.c_str()); return 2; }
    std::ifstream file;
    std::istream *in = &std::cin;
    if (a.size() > 1 && a[1] != "-") {
        file.open(a[1]);
        if (!file) { std::fprintf(stderr, "adlib: cannot open %s\n", a[1].c_str()); return 2; }
        in = &file;
    }
    std::map<std::string, bool> ctx;
    std::string line;
    size_t n = 0;
    while (std::getline(*in, line)) {
        size_t p = line.find("[tick ");
        if (p == std::string::npos) continue;
        std::string l = line.substr(p);
        if (traceLineMatches(f, l, ctx)) { std::puts(l.c_str()); ++n; }
    }
    return n ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    SymbolTable syms;
    std::string err, cmd, enumIds, anims, sounds, texts;
    // Vocabulary options may appear before or after the command; everything else is kept.
    std::vector<std::string> keep;
    for (size_t k = 0; k < args.size(); ++k) {
        const std::string &x = args[k];
        bool hasV = k + 1 < args.size();
        bool ok = true;
        if (x == "-h" || x == "--help") { std::fputs(kUsage, stdout); return 0; }
        if (x == "--version") { std::printf("adlib %s\n", adlib::kVersion); return 0; }
        if (x == "--symbols" && hasV) ok = loadSymbolsDir(syms, args[++k], &err);
        else if (x == "--enumids" && hasV) enumIds = args[++k];
        else if (x == "--anims" && hasV) anims = args[++k];
        else if (x == "--sounds" && hasV) sounds = args[++k];
        else if (x == "--texts" && hasV) texts = args[++k];
        else if (x == "--enum" && hasV) ok = loadEnumFile(syms, args[++k], &err);
        else if (x == "--hint" && hasV) ok = syms.addHint(args[++k], &err);
        else if (x == "--list" && hasV) {
            std::string v = args[++k];
            size_t eq = v.find('=');
            ok = eq != std::string::npos && loadListFile(syms, v.substr(0, eq), v.substr(eq + 1), &err);
        } else if (x == "--plans-dir") keep.push_back("--plans"); // adlibc spelling
        else if (cmd.empty() && !x.empty() && x[0] != '-') cmd = x;
        else keep.push_back(x);
        if (!ok) { std::fprintf(stderr, "adlib: %s\n", err.empty() ? "bad vocabulary option" : err.c_str()); return 2; }
    }
    if (!enumIds.empty() && !loadCompilerSymbolFiles(syms, enumIds, anims, sounds, texts, &err)) {
        std::fprintf(stderr, "adlib: %s\n", err.c_str());
        return 2;
    }
    if (cmd.empty()) return usage();
    if (cmd == "dump") return cmdDump(syms, keep);
    if (cmd == "decompile") return cmdDecompile(syms, keep);
    if (cmd == "graph") return cmdGraph(syms, keep);
    if (cmd == "inspect" || cmd == "lint") {
        if (cmd == "lint") keep.push_back("--lint-only");
        return cmdInspect(syms, keep);
    }
    if (cmd == "compare" || cmd == "diff") return cmdCompare(syms, keep);
    if (cmd == "trace") return cmdTrace(keep);
    std::fprintf(stderr, "adlib: unknown command %s\n", cmd.c_str());
    return usage();
}
