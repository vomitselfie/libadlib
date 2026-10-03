// adlib-tools: symbol tables (the compiler's view of a vocabulary).
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "adlib/tools.h"

namespace adlib::tools {

namespace {

std::string lower(std::string_view s) {
    std::string r(s);
    for (char &c : r) c = char(std::tolower(uint8_t(c)));
    return r;
}

bool readFile(const std::string &path, std::string &out, std::string *err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// The exe tokenizer (CPlanCompiler_NextToken 0x489940 and helpers), as used by LoadSymbols.
// The 1996 compiler's tokenizer for symbol files (CPlanCompiler_LoadSymbols).
struct Tok {
    const std::string &b;
    size_t pos = 0;
    explicit Tok(const std::string &d) : b(d) {}
    char c(size_t o = 0) const { size_t p = pos + o; return p < b.size() ? b[p] : '\0'; }
    bool sep() const { char x = c(); return x == ' ' || x == '\t' || x == ',' || x == '&'; }
    bool cmt() const {
        char x = c();
        return (x == '/' && (c(1) == '/' || c(1) == '*')) || x == '(' || x == '#';
    }
    static bool nl(char x) { return x == '\r' || x == '\n' || x == '\0'; }
    void skipLine() {
        while (!nl(c())) ++pos;
        if (c() == '\r') ++pos;
        while (!nl(c())) ++pos;
        if (c() == '\n') ++pos;
    }
    bool eof() const { return pos > b.size() || c() == '\0'; }
    std::string next() {
        std::string t;
        while (sep()) ++pos;
        while (true) {
            if (!t.empty()) return t;
            bool s = false, cm = false, n = false, br = false;
            while (true) {
                if (sep()) { s = true; break; }
                if (cmt()) { cm = true; break; }
                char ch = c();
                if (ch == '\r' || ch == '\n') { n = true; break; }
                br = ch == '}';
                if (br || eof()) break;
                t += ch;
                ++pos;
            }
            if (s) while (sep()) ++pos;
            if (cm || n) skipLine();
            if (br && t.empty()) { ++pos; return "}"; }
            if (eof()) return t;
        }
    }
};

std::string upperStem(const std::filesystem::path &p) {
    std::string s = p.stem().string();
    for (char &c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

} // namespace

SymbolTable::SymbolTable() : vocab_(std::make_shared<Vocabulary>()) {
    argHints[{"SendMessage", 2}] = "@Message";
}

bool SymbolTable::isMessageArg(const std::string &acf, int i) const {
    auto it = argHints.find({acf, i});
    return it != argHints.end() && it->second == "@Message";
}

void SymbolTable::add(const std::string &name, int value, const std::string &ns) {
    if (name.empty()) return;
    entries_.push_back({name, value, ns});
    first_.emplace(lower(name), entries_.size() - 1);
    vocab_->ns(ns).add(name, value);
}

const SymbolTable::Entry *SymbolTable::lookup(std::string_view name) const {
    auto it = first_.find(lower(name));
    return it == first_.end() ? nullptr : &entries_[it->second];
}

bool SymbolTable::isIdentifier(std::string_view n) {
    auto starts = [&](const char *p) { return n.substr(0, std::char_traits<char>::length(p)) == p; };
    return starts("id") || starts("_") || starts("A_") || starts("AA_");
}

bool SymbolTable::isPronounName(std::string_view n) { return n.size() >= 6 && n.substr(3, 3) == "PRN"; }

std::string SymbolTable::nameIn(const std::string &ns, int v) const {
    std::string nsName = ns;
    if (!ns.empty() && ns[0] == '@') { // role reference
        for (int r = 0; r < int(Role::Count); ++r)
            if (ns.substr(1) == adlib::roleName(Role(r))) nsName = vocab_->binding(Role(r));
    }
    const Namespace *n = vocab_->find(nsName);
    if (!n) return {};
    const char *nm = n->name(v);
    if (!nm || !*nm) return {};
    if (!isIdentifier(nm) || isPronounName(nm)) return {};
    const Entry *e = lookup(nm);
    return e && e->value == v ? std::string(nm) : std::string();
}

std::string SymbolTable::roleName(Role r, int v) const { return nameIn(vocab_->binding(r), v); }

std::string SymbolTable::displayName(Role r, int v) const {
    const char *n = vocab_->name(r, v);
    if (n && *n) return Namespace::stripPrefix(n);
    return std::to_string(v);
}

bool SymbolTable::addHint(const std::string &spec, std::string *err) {
    // ACTION:INDEX=NAMESPACE
    size_t c = spec.find(':'), e = spec.find('=');
    if (c == std::string::npos || e == std::string::npos || e < c) {
        if (err) *err = "bad hint (want Action:index=Namespace): " + spec;
        return false;
    }
    std::string acf = Namespace::stripPrefix(spec.substr(0, c));
    argHints[{acf, std::atoi(spec.substr(c + 1, e - c - 1).c_str())}] = spec.substr(e + 1);
    return true;
}

bool loadCompilerSymbolsDir(SymbolTable &t, const std::string &dataDir, std::string *err) {
    namespace fs = std::filesystem;
    // Case-insensitive file lookup (the 1996 layout comes from a DOS file system).
    auto find = [&](std::initializer_list<const char *> parts, std::string &out) {
        fs::path p = dataDir;
        for (const char *part : parts) {
            fs::path hit;
            std::error_code ec;
            if (fs::is_directory(p, ec))
                for (auto &de : fs::directory_iterator(p, ec))
                    if (lower(de.path().filename().string()) == lower(part)) { hit = de.path(); break; }
            if (hit.empty()) return false;
            p = hit;
        }
        out = p.string();
        return true;
    };
    std::string e, a, s, x;
    if (!find({"enumIDs.h"}, e)) {
        if (err) *err = "no enumIDs.h in " + dataDir;
        return false;
    }
    find({"ANIMDATA", "ANIMASSM.TXT"}, a);
    find({"SNDFILES.LST"}, s);
    find({"INFOBTXT.LST"}, x);
    return loadCompilerSymbolFiles(t, e, a, s, x, err);
}

bool loadCompilerSymbolFiles(SymbolTable &t, const std::string &enumIds, const std::string &animAssm,
                             const std::string &sndFiles, const std::string &infobTxt, std::string *err) {
    std::string path = enumIds, data;
    if (!readFile(path, data, err)) return false;
    // Role bindings / namespace names come from the regular enum parser (same ids, plans_test).
    Vocabulary names;
    if (!names.loadEnumHeader(data, err)) return false;
    std::vector<std::string> enumOrder = names.namespaceNames();
    {
        Tok k(data);
        while (k.c() != '{' && !k.eof()) ++k.pos;
        ++k.pos;
        int v = 0;
        size_t e = 0;
        while (k.pos <= k.b.size() && k.c() != '\0') {
            std::string w = k.next();
            if (w == "}") {
                v = 0;
                ++e;
                if (k.c() != '{') {
                    while (!k.eof()) {
                        ++k.pos;
                        if (k.c() == '{') break;
                    }
                }
                ++k.pos;
            } else if (!w.empty()) {
                t.add(w, v++, e < enumOrder.size() ? enumOrder[e] : "ENUM" + std::to_string(e));
            }
        }
    }
    if (!animAssm.empty() && readFile(animAssm, data, err)) {
        Tok k(data);
        k.skipLine();
        int v = 0;
        while (k.pos <= k.b.size() && k.c() != '\0') {
            std::string w = k.next();
            if (w.rfind("AA_", 0) == 0) t.add(w, v++, "AnimAssm");
            else k.skipLine();
        }
    }
    if (!sndFiles.empty() && readFile(sndFiles, data, err)) {
        Tok k(data);
        int v = 0;
        while (k.pos <= k.b.size() && k.c() != '\0') {
            if (k.cmt()) k.skipLine();
            std::string w = k.next();
            char f = w.empty() ? '\0' : w[0];
            if (!(std::isdigit(uint8_t(f)) || f == '.' || f == '+' || f == '-')) t.add("id_SND_" + w, v++, "Sounds");
            else k.skipLine();
        }
    }
    if (!infobTxt.empty() && readFile(infobTxt, data, err)) {
        Tok k(data);
        int v = 0;
        while (k.pos <= k.b.size() && k.c() != '\0') {
            if (!k.cmt()) t.add("id_TXT_" + k.next(), v++, "InfoText");
            k.skipLine();
        }
    }
    if (err) err->clear();
    return true;
}

bool loadEnumFile(SymbolTable &t, const std::string &path, std::string *err) {
    std::string data;
    if (!readFile(path, data, err)) return false;
    Vocabulary v;
    if (!v.loadEnumHeader(data, err)) {
        if (err) *err = path + ": " + *err;
        return false;
    }
    for (const std::string &ns : v.namespaceNames()) {
        const Namespace *n = v.find(ns);
        for (int i = 0; i < n->size(); ++i)
            if (!n->names()[size_t(i)].empty()) t.add(n->names()[size_t(i)], i, ns);
    }
    return true;
}

bool loadListFile(SymbolTable &t, const std::string &ns, const std::string &path, std::string *err) {
    std::string data;
    if (!readFile(path, data, err)) return false;
    Vocabulary v;
    v.loadList(ns, data);
    const Namespace *n = v.find(ns);
    for (int i = 0; n && i < n->size(); ++i) t.add(n->names()[size_t(i)], i, ns);
    return true;
}

bool loadSymbolsDir(SymbolTable &t, const std::string &dir, std::string *err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        if (err) *err = "not a directory: " + dir;
        return false;
    }
    bool hasEnumIds = false, hasAnim = false;
    std::vector<fs::path> headers, lists;
    for (auto &de : fs::directory_iterator(dir, ec)) {
        std::string n = lower(de.path().filename().string());
        std::string ext = lower(de.path().extension().string());
        if (n == "enumids.h") hasEnumIds = true;
        if (n == "animdata" && de.is_directory()) hasAnim = true;
        if (de.is_regular_file() && ext == ".h") headers.push_back(de.path());
        if (de.is_regular_file() && ext == ".lst") lists.push_back(de.path());
    }
    if (hasEnumIds && hasAnim) return loadCompilerSymbolsDir(t, dir, err);
    std::sort(headers.begin(), headers.end());
    std::sort(lists.begin(), lists.end());
    if (headers.empty()) {
        if (err) *err = "no enum headers (*.H) in " + dir;
        return false;
    }
    for (auto &h : headers)
        if (!loadEnumFile(t, h.string(), err)) return false;
    for (auto &l : lists)
        if (!loadListFile(t, upperStem(l), l.string(), err)) return false;
    return true;
}

} // namespace adlib::tools
