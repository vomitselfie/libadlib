// adlibc: compile ADLIB plan source (.txt) to a .PBN, byte-identical to the 1996 ADLIB compiler.
// See docs/adlibc.md.
//
//   adlibc [--symbols DIR | --enumids F [--anims F] [--sounds F] [--texts F]]
//          [--compat=adlib|adlib1996] [--plans-dir DIR] [--strict] [--Werror] [-o out.pbn] [-q] file.txt
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>

#include "adlib/version.h"
#include "adlib/compiler.h"
#include "adlib/pbn.h"
#ifdef ADLIBC_LINT
#include "adlib/tools.h"
#endif

using namespace adlib;

namespace {

void usage() {
    std::fprintf(stderr,
        "usage: adlibc [symbols] [--compat=MODE] [--plans-dir DIR] [--strict] [--Werror] [-o OUT.pbn] [-q] FILE.txt\n"
        "symbols (the four symbol files, read the way the 1996 compiler read them):\n"
        "  --symbols DIR           a directory in the 1996 layout: enumIDs.h, AnimData/Animassm.txt,\n"
        "                          Sndfiles.lst, Infobtxt.lst (names case-insensitive; any but\n"
        "                          enumIDs.h may be missing)\n"
        "  --enumids F --anims F --sounds F --texts F   individual files (any may be omitted)\n"
        "compatibility (docs/adlibc.md):\n"
        "  --compat=adlib          (default) the ADLIB language, any vocabulary, with the 1996\n"
        "                          implementation limits lifted\n"
        "  --compat=adlib1996      the 1996 compiler exactly, including its fixed limits; input it\n"
        "                          would crash or hang on is an error, mangled-but-accepted input warns\n"
        "options:\n"
        "  --plans-dir DIR   where CHANGE_OF_PLAN finds sibling plans (default: FILE's directory)\n"
        "  --strict          warn about input the original accepted silently (output unchanged)\n"
        "  --Werror          strict warnings are errors (no output is written)\n"
        "  -o OUT            output file (default: <plan name>.pbn in the current directory)\n"
        "  -q                no summary line\n"
        "  --version         print the version\n");
}

bool readFile(const std::string &p, std::string &out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string lower(std::string s) {
    for (char &c : s) if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return s;
}

std::optional<std::string> findPlan(const std::string &dir, const std::string &name) {
    DIR *h = opendir(dir.empty() ? "." : dir.c_str());
    if (!h) return std::nullopt;
    std::string want = lower(name + ".txt"), found;
    while (dirent *e = readdir(h))
        if (lower(e->d_name) == want) { found = e->d_name; break; }
    closedir(h);
    if (found.empty()) return std::nullopt;
    std::string text;
    if (!readFile((dir.empty() ? "." : dir) + "/" + found, text)) return std::nullopt;
    return text;
}

} // namespace

int main(int argc, char **argv) {
    std::string symDir, outPath, plansDir, input;
    bool haveFiles = false, strict = false, werror = false, quiet = false, adlib1996 = false;
    SymbolFiles files;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&](std::string &dst) {
            if (i + 1 >= argc) { usage(); std::exit(2); }
            dst = argv[++i];
        };
        auto fileArg = [&](std::optional<std::string> &dst) {
            std::string p, t;
            val(p);
            if (!readFile(p, t)) { std::fprintf(stderr, "adlibc: cannot read %s\n", p.c_str()); std::exit(2); }
            dst = t;
            haveFiles = true;
        };
        if (a == "--compat=adlib1996") adlib1996 = true;
        else if (a == "--compat=adlib") adlib1996 = false;
        else if (a.rfind("--compat=", 0) == 0) { std::fprintf(stderr, "adlibc: unknown mode %s (adlib, adlib1996)\n", a.c_str()); return 2; }
        else if (a == "--symbols") val(symDir);
        else if (a == "--enumids") fileArg(files.enumIds);
        else if (a == "--anims") fileArg(files.animAssm);
        else if (a == "--sounds") fileArg(files.sndFiles);
        else if (a == "--texts") fileArg(files.infobTxt);
        else if (a == "--plans-dir") val(plansDir);
        else if (a == "--strict") strict = true;
        else if (a == "--Werror" || a == "-Werror") { werror = true; strict = true; }
        else if (a == "-o") val(outPath);
        else if (a == "-q") quiet = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a == "--version") { std::printf("adlibc %s\n", adlib::kVersion); return 0; }
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "adlibc: unknown option %s\n", a.c_str()); usage(); return 2; }
        else input = a;
    }
    if (input.empty() || (symDir.empty() && !haveFiles)) { usage(); return 2; }

    // --compat=adlib1996 enforces the original loader's limits on the symbol files; otherwise the
    // headers are taken as written and anything the 1996 loader would have mishandled is reported
    // under --strict.
    CompilerSymbols syms;
    try {
        if (!symDir.empty()) {
            std::string err;
            syms = CompilerSymbols::fromDataDir(symDir, &err, adlib1996);
            if (!err.empty()) { std::fprintf(stderr, "adlibc: %s\n", err.c_str()); return 2; }
        } else {
            syms = CompilerSymbols::fromFiles(files, adlib1996);
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "adlibc: %s\n", e.what());
        return 2;
    }
    if (strict)
        for (const std::string &w : syms.loadWarnings()) std::fprintf(stderr, "adlibc: warning: %s [symbols]\n", w.c_str());

    std::string source;
    if (!readFile(input, source)) { std::fprintf(stderr, "adlibc: cannot read %s\n", input.c_str()); return 2; }
    std::string base = input.substr(input.find_last_of('/') + 1);
    std::string name = base.substr(0, base.find_last_of('.'));
    if (plansDir.empty()) {
        size_t s = input.find_last_of('/');
        plansDir = s == std::string::npos ? "." : input.substr(0, s);
    }
    CompileOptions opt;
    opt.compat = adlib1996 ? Compat::Adlib1996 : Compat::Adlib;
    opt.strict = strict;
    opt.werror = werror;
    opt.planSource = [&](const std::string &plan) -> std::optional<std::string> {
        if (lower(plan) == lower(name)) return source; // the file being compiled, as given
        return findPlan(plansDir, plan);
    };
    CompileResult r = compilePlan(source, syms, opt);
    // Tree-level lint (arities, interactor options, action lists): the shared adlib-tools rules.
    std::vector<std::string> lint;
#ifdef ADLIBC_LINT
    if (strict && !r.pbn.empty()) {
        std::vector<uint32_t> words;
        std::string err;
        PlanTree tree;
        if (readPbnWords(r.pbn, words, &err) && buildPlanTree(words, LoadHooks{}, tree, &err)) {
            tools::SymbolTable st;
            for (const CompilerSymbol &s : syms.symbols()) st.add(s.name, s.value, s.group);
            for (const tools::Issue &i : tools::lintPlan(tree, &st, &r.words))
                if (i.severity != tools::Issue::Severity::Info)
                    lint.push_back(input + ": warning: " + i.path + ": " + i.message + " [lint]");
        }
        if (werror && !lint.empty()) r.ok = false;
    }
#endif
    for (const std::string &l : lint) std::fprintf(stderr, "%s\n", l.c_str());
    for (const Diagnostic &d : r.diagnostics) {
        std::fprintf(stderr, "%s\n", d.format(input).c_str());
        if (d.severity == Diagnostic::Severity::Error && (d.kind == "crash" || d.kind == "corrupt" ||
                                                          d.kind == "hang" || d.kind == "undefined"))
            std::fprintf(stderr, "%s: note: adlibc rejects this input because the original compiler "
                                 "would %s on it\n", input.c_str(),
                         d.kind == "crash" ? "crash" : d.kind == "hang" ? "hang" :
                         d.kind == "corrupt" ? "corrupt its memory" : "produce an undefined value");
    }
    if (!r.ok) {
        if (r.error() == nullptr && werror) std::fprintf(stderr, "adlibc: warnings treated as errors (--Werror)\n");
        return 1;
    }
    if (outPath.empty()) outPath = name + ".pbn";
    std::ofstream f(outPath, std::ios::binary);
    if (!f || !f.write(reinterpret_cast<const char *>(r.pbn.data()), std::streamsize(r.pbn.size()))) {
        std::fprintf(stderr, "adlibc: cannot write %s\n", outPath.c_str());
        return 1;
    }
    if (!quiet) std::fprintf(stderr, "wrote %s (%zu bytes)\n", outPath.c_str(), r.pbn.size());
    return 0;
}
