// adlibc tests (ctest adlibc_test FIXTURES), no external data:
//   - fixtures/compiler/{valid,quirks,errors,fuzz-regressions}: the compatibility corpus. Every
//     expectation was produced by the original 1996 compiler run with the symbol files in
//     fixtures/vocab (or is adlibc's refusal where the original crashes); adlibc --compat=adlib1996
//     must give the same outcome, the exact diagnostic, PBN sha256 and every dword;
//   - unit checks of behaviour discovered with the original compiler;
//   - the vending machine (new-game path): symbols from files and from a Vocabulary agree, and the
//     build-time PBNs match.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "adlib/compiler.h"
#include "adlib/pbn.h"
#include "adlib/vocabulary.h"

using namespace adlib;


namespace {

int failures = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++failures; std::printf("FAIL %s:%d: %s ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

std::string gFixtures;

bool readFile(const std::string &p, std::string &out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
std::vector<std::string> listDir(const std::string &d, const std::string &ext) {
    std::vector<std::string> v;
    if (DIR *h = opendir(d.c_str())) {
        while (dirent *e = readdir(h)) {
            std::string n = e->d_name;
            if (n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0) v.push_back(n);
        }
        closedir(h);
    }
    std::sort(v.begin(), v.end());
    return v;
}
std::string lower(std::string s) { for (char &c : s) if (c >= 'A' && c <= 'Z') c = char(c + 32); return s; }
std::optional<std::string> findCI(const std::string &dir, const std::string &file) {
    for (const std::string &n : listDir(dir, ""))
        if (lower(n) == lower(file)) { std::string t; if (readFile(dir + "/" + n, t)) return t; }
    return std::nullopt;
}

// ---- SHA-256 -----------------------------------------------------------------------------------
std::string sha256(const std::string &data) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string m = data;
    uint64_t bits = uint64_t(data.size()) * 8;
    m.push_back(char(0x80));
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back(char(bits >> (i * 8)));
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = uint32_t(uint8_t(m[off + 4 * i])) << 24 | uint32_t(uint8_t(m[off + 4 * i + 1])) << 16 |
                   uint32_t(uint8_t(m[off + 4 * i + 2])) << 8 | uint32_t(uint8_t(m[off + 4 * i + 3]));
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + 8 * i, 9, "%08x", h[i]);
    return out;
}
std::string sha256(const std::vector<uint8_t> &d) { return sha256(std::string(d.begin(), d.end())); }

// ---- minimal JSON ------------------------------------------------------------------------------
struct Json {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;
    const Json &operator[](const std::string &k) const {
        static const Json none;
        for (auto &p : o) if (p.first == k) return p.second;
        return none;
    }
};
struct JsonParser {
    const std::string &s;
    size_t i = 0;
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    static void utf8(std::string &out, uint32_t cp) {
        if (cp < 0x80) out.push_back(char(cp));
        else if (cp < 0x800) { out.push_back(char(0xC0 | cp >> 6)); out.push_back(char(0x80 | (cp & 63))); }
        else if (cp < 0x10000) { out.push_back(char(0xE0 | cp >> 12)); out.push_back(char(0x80 | ((cp >> 6) & 63))); out.push_back(char(0x80 | (cp & 63))); }
        else { out.push_back(char(0xF0 | cp >> 18)); out.push_back(char(0x80 | ((cp >> 12) & 63))); out.push_back(char(0x80 | ((cp >> 6) & 63))); out.push_back(char(0x80 | (cp & 63))); }
    }
    std::string str() {
        std::string out;
        ++i; // "
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') { out.push_back(c); continue; }
            char e = s[i++];
            switch (e) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'u': {
                uint32_t cp = uint32_t(std::stoul(s.substr(i, 4), nullptr, 16));
                i += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && s[i] == '\\' && s[i + 1] == 'u') {
                    uint32_t lo = uint32_t(std::stoul(s.substr(i + 2, 4), nullptr, 16));
                    i += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(out, cp);
                break;
            }
            default: out.push_back(e);
            }
        }
        ++i;
        return out;
    }
    Json value() {
        ws();
        Json j;
        if (s[i] == '{') {
            j.t = Json::Obj; ++i; ws();
            if (s[i] == '}') { ++i; return j; }
            for (;;) { ws(); std::string k = str(); ws(); ++i; j.o.push_back({k, value()}); ws(); if (s[i++] == '}') break; }
        } else if (s[i] == '[') {
            j.t = Json::Arr; ++i; ws();
            if (s[i] == ']') { ++i; return j; }
            for (;;) { j.a.push_back(value()); ws(); if (s[i++] == ']') break; }
        } else if (s[i] == '"') {
            j.t = Json::Str; j.s = str();
        } else if (s.compare(i, 4, "null") == 0) { i += 4; }
        else if (s.compare(i, 4, "true") == 0) { j.t = Json::Bool; j.b = true; i += 4; }
        else if (s.compare(i, 5, "false") == 0) { j.t = Json::Bool; i += 5; }
        else { j.t = Json::Num; size_t e; j.n = std::stod(s.substr(i), &e); i += e; }
        return j;
    }
};
Json parseJson(const std::string &s) { JsonParser p{s}; return p.value(); }

// UTF-8 (as stored in the probe log) -> the cp1252 bytes the probe fed to the original.
std::string toCp1252(const std::string &u) {
    static const std::map<uint32_t, uint8_t> hi = {
        {0x20AC, 0x80}, {0x201A, 0x82}, {0x0192, 0x83}, {0x201E, 0x84}, {0x2026, 0x85}, {0x2020, 0x86},
        {0x2021, 0x87}, {0x02C6, 0x88}, {0x2030, 0x89}, {0x0160, 0x8A}, {0x2039, 0x8B}, {0x0152, 0x8C},
        {0x017D, 0x8E}, {0x2018, 0x91}, {0x2019, 0x92}, {0x201C, 0x93}, {0x201D, 0x94}, {0x2022, 0x95},
        {0x2013, 0x96}, {0x2014, 0x97}, {0x02DC, 0x98}, {0x2122, 0x99}, {0x0161, 0x9A}, {0x203A, 0x9B},
        {0x0153, 0x9C}, {0x017E, 0x9E}, {0x0178, 0x9F}};
    std::string out;
    for (size_t i = 0; i < u.size();) {
        uint8_t c = uint8_t(u[i]);
        uint32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else { cp = c & 0x07; n = 4; }
        for (int k = 1; k < n; ++k) cp = (cp << 6) | (uint8_t(u[i + size_t(k)]) & 0x3F);
        i += size_t(n);
        if (cp < 0x100 && !(cp >= 0x80 && cp < 0xA0)) out.push_back(char(cp));
        else { auto it = hi.find(cp); out.push_back(it != hi.end() ? char(it->second) : '?'); }
    }
    return out;
}

std::string hex8(uint32_t v) { char b[9]; std::snprintf(b, sizeof b, "%08x", v); return b; }

CompileOptions dirOptions(const std::string &dir, const std::string *self = nullptr, const std::string &selfName = "") {
    CompileOptions o;
    o.compat = Compat::Adlib1996;
    o.planSource = [dir, self, selfName](const std::string &n) -> std::optional<std::string> {
        if (self && lower(n) == lower(selfName)) return *self;
        return findCI(dir, n + ".txt");
    };
    return o;
}

// ---- the tests ---------------------------------------------------------------------------------

void testCorpus(const CompilerSymbols &syms, const std::string &base, const char *label) {
    std::string plansDir = gFixtures + "/compiler/plans";
    std::map<std::string, std::pair<int, int>> per;
    for (const char *kind : {"valid", "quirks", "errors", "fuzz-regressions"}) {
        std::string dir = base + "/" + kind;
        for (const std::string &f : listDir(dir, ".txt")) {
            std::string src, exps;
            readFile(dir + "/" + f, src);
            CHECK(readFile(dir + "/" + f.substr(0, f.size() - 4) + ".expected.json", exps), "%s", f.c_str());
            Json exp = parseJson(exps);
            if (exp["outcome"].t != Json::Str) { CHECK(false, "%s/%s: not frozen (freeze it with the original compiler)", kind, f.c_str()); continue; }
            std::string self = f.substr(0, f.size() - 4);
            CompileOptions o = dirOptions(plansDir, &src, self);
            CompileResult r = compilePlan(src, syms, o);
            bool ok;
            if (exp["outcome"].s == "rejected") {
                const Diagnostic *d = r.error();
                // expectations from the original compiler keep its raw message; adlibc's own are printed lines
                bool fromAdlibc = exp["expected_from"].s.rfind("adlibc", 0) == 0;
                std::string got = d ? d->format(f, fromAdlibc) : "(accepted)";
                ok = !r.ok && got == exp["diagnostic"].s;
                CHECK(ok, "%s/%s: got '%s' expected '%s'", kind, f.c_str(), got.c_str(), exp["diagnostic"].s.c_str());
            } else {
                ok = !r.pbn.empty() && sha256(r.pbn) == exp["pbn_sha256"].s;
                if (ok && exp["pbn_words"].t == Json::Arr) {
                    std::vector<uint32_t> w;
                    for (size_t i = 0; i + 3 < r.pbn.size(); i += 4)
                        w.push_back(uint32_t(r.pbn[i]) | uint32_t(r.pbn[i + 1]) << 8 | uint32_t(r.pbn[i + 2]) << 16 | uint32_t(r.pbn[i + 3]) << 24);
                    ok = w.size() == exp["pbn_words"].a.size();
                    for (size_t i = 0; ok && i < w.size(); ++i) ok = hex8(w[i]) == exp["pbn_words"].a[i].s;
                }
                CHECK(ok, "%s/%s: PBN differs%s", kind, f.c_str(), r.error() ? (": " + r.error()->format(f)).c_str() : "");
            }
            per[kind].first += ok;
            per[kind].second += 1;
        }
    }
    for (auto &[k, v] : per) std::printf("%s %-16s %d/%d agree\n", label, k.c_str(), v.first, v.second);
}

void testVending() {
    std::string dir = VENDING_DIR;
    SymbolFiles files;
    std::string h, l;
    CHECK(readFile(dir + "/VENDING.H", h) && readFile(dir + "/VENDTEXT.LST", l), "vending files");
    files.enumIds = h;
    files.infobTxt = l;
    CompilerSymbols fs = CompilerSymbols::fromFiles(files);
    CHECK(fs.loadWarnings().empty(), "VENDING.H load warnings");
    CHECK(fs.find("id_TXT_T_Welcome") && fs.find("id_TXT_T_Welcome")->value == 0, "texts list");
    // The same enums through a libadlib Vocabulary.
    Vocabulary v;
    std::string err;
    CHECK(v.loadEnumHeader(h, &err), "%s", err.c_str());
    v.loadList("Texts", l);
    Vocabulary v2;
    CHECK(v2.loadEnumHeader(h, &err), "%s", err.c_str());
    {   // texts with the Infobtxt naming, as the original's loader names them
        std::istringstream ls(l);
        std::string line;
        Namespace &t = v2.ns("Infobtxt");
        while (std::getline(ls, line)) {
            if (line.empty() || line[0] == '#') continue;
            t.append("id_TXT_" + line.substr(0, line.find_first_of(" \t\r")));
        }
    }
    CompilerSymbols vs = CompilerSymbols::fromVocabulary(v2);
    for (const CompilerSymbol &s : fs.symbols()) {
        const CompilerSymbol *o = vs.find(s.name);
        CHECK(o && o->value == s.value, "vocabulary symbol %s", s.name.c_str());
    }
    for (const char *plan : {"VENDING", "CUSTOMER"}) {
        std::string src, built;
        CHECK(readFile(dir + "/" + plan + ".txt", src), "%s.txt", plan);
        CompileOptions o;
        o.strict = true;
        CompileResult a = compilePlan(src, fs, o), b = compilePlan(src, vs, o);
        CHECK(a.ok && b.ok && a.pbn == b.pbn, "%s: files vs vocabulary", plan);
        for (const Diagnostic &d : a.diagnostics) CHECK(false, "%s: unexpected %s", plan, d.format(plan).c_str());
        if (readFile(std::string(VENDING_PBN_DIR) + "/" + plan + ".PBN", built))
            CHECK(std::string(a.pbn.begin(), a.pbn.end()) == built, "%s: build-time PBN differs", plan);
        // the plan loads as a tree
        std::vector<uint32_t> w;
        PlanTree t;
        CHECK(readPbnWords(a.pbn, w, &err) && buildPlanTree(w, LoadHooks{}, t, &err), "%s: %s", plan, err.c_str());
    }
    std::printf("vending machine: plans compile from ADLIB source (files and Vocabulary agree)\n");
}

void testUnits(const CompilerSymbols *base) {
    // Behaviour discovered with the original compiler that the docs did not cover (see docs/adlibc.md).
    CompilerSymbols s;
    if (base) s = *base;
    CompileOptions H, A; // --compat=adlib1996 / --compat=adlib
    H.compat = Compat::Adlib1996;
    auto words = [&](const std::string &src, const CompileOptions &o) {
        CompileResult r = compilePlan(src, s, o);
        std::vector<uint32_t> w;
        for (size_t i = 4; i + 3 < r.pbn.size(); i += 4)
            w.push_back(uint32_t(r.pbn[i]) | uint32_t(r.pbn[i + 1]) << 8 | uint32_t(r.pbn[i + 2]) << 16 | uint32_t(r.pbn[i + 3]) << 24);
        return std::make_pair(r, w);
    };
    for (const CompileOptions *o : {&H, &A}) {
        // "1.5d2" (Fortran-style exponent, accepted by MSVC's atof) and ".5e" (empty exponent).
        auto [r1, w1] = words("1.5d2 .5e 1e3 END_PLAN\n", *o);
        CHECK(w1.size() >= 3 && w1[0] == 0x43160000 && w1[1] == 0x3F000000 && w1[2] == 1, "numbers");
        // Bad id / bad number messages carry only the first 20 characters of the token.
        CompileResult r3 = compilePlan("12345678901234567890123z END_PLAN\n", s, *o);
        CHECK(r3.error() && r3.error()->message == "Bad number in plan: 12345678901234567890", "%s",
              r3.error() ? r3.error()->message.c_str() : "");
    }
    // "0x" alone: the original emits stale stack bytes -> adlib1996 rejects, adlib emits 0.
    CompileResult r2 = compilePlan("0x END_PLAN\n", s, H);
    CHECK(!r2.ok && r2.error() && r2.error()->kind == "undefined", "0x adlib1996");
    auto [r2a, w2a] = words("0x END_PLAN\n", A);
    CHECK(r2a.ok && !w2a.empty() && w2a[0] == 0, "0x adlib");
    // An unterminated string is finished with stale bytes of an earlier, longer string token
    // (adlib1996, with a warning); adlib reports the original's own error for it.
    auto [r4, w4] = words("\"Longer\" \"ab END_PLAN\n", H);
    CHECK(r4.ok && w4.size() > 12 && w4[9] == 'a' && w4[10] == 'b' && w4[11] == 0 && w4[12] == 'g', "stale string");
    bool warned = false;
    for (const Diagnostic &d : r4.diagnostics) warned |= d.code == "adlib1996-stale-string";
    CHECK(warned, "stale string warning");
    CompileResult r4a = compilePlan("\"Longer\" \"ab END_PLAN\n", s, A);
    CHECK(!r4a.ok && r4a.error() && r4a.error()->message == "Inability to read string: \"ab", "stale string adlib");
    // String tokens of 40 bytes crash the original: adlib1996 rejects, adlib emits the string.
    std::string longStr = "\"" + std::string(50, 'x') + "\" END_PLAN\n";
    CompileResult r5 = compilePlan(longStr, s, H);
    CHECK(!r5.ok && r5.error() && r5.error()->kind == "crash", "40-byte string adlib1996");
    auto [r5a, w5a] = words(longStr, A);
    CHECK(r5a.ok && w5a.size() == 1 + 50 + 1 + 2, "long string adlib (%zu words)", w5a.size());
    // 61 behaviours: the 61st name lands in the original's local-real table (adlib1996 warns and
    // reproduces it: "B60", even in its own declaration, then compiles as local real 0, 1 dword
    // instead of a 5-dword string); adlib keeps 61 behaviours.
    std::string many;
    for (int i = 0; i <= 60; ++i) many += "DECLARE_BEHAVIOR 2 _1_ \"B" + std::to_string(i) + "\"\n";
    many += "CHANGE_TO_BEHAVIOR 1 \"B60\" END_PLAN\n";
    auto [r6, w6] = words(many, H);
    auto [r6a, w6a] = words(many, A);
    CHECK(r6.ok && r6a.ok && w6.size() + 4 == w6a.size(), "61 behaviours");
    CHECK(!w6.empty() && w6[w6.size() - 3] == 0xEDCB0000u && w6a[w6a.size() - 3] == 60, "61st behaviour: %08x %08x",
          w6.empty() ? 0 : w6[w6.size() - 3], w6a.empty() ? 0 : w6a[w6a.size() - 3]);
    std::printf("units: ok\n");
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: adlibc_test FIXTURES\n"); return 2; }
    gFixtures = argv[1];
    std::string err;
    CompilerSymbols ts = CompilerSymbols::fromDataDir(gFixtures + "/vocab", &err, true);
    CHECK(err.empty(), "fixtures/vocab: %s", err.c_str());
    CHECK(ts.find("id_ACF_Probe") && ts.find("AA_WalkToRun") && ts.find("id_SND_Whistle1") && ts.find("id_TXT_Notice"),
          "fixtures/vocab: the four symbol files");
    testCorpus(ts, gFixtures + "/compiler", "compiler corpus");
    testUnits(&ts);
    testVending();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
