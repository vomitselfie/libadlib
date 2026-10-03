#include "adlib/vocabulary.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace adlib {

namespace {
const char *const kRoleNames[] = {"Message", "CollisionObject", "Behavior", "Agenda", "Action",
                                  "Decision", "BehaviorParam", "InteractorParam", "Pronoun", "Object"};
const char *const kRoleEnums[] = {"ID_MSG", "CollisionObjectID", "Behavior_IDs", "AgendaItemIDs",
                                  "ActionFunctionIDs", "DecisionFunctionIDs", "BehaviorSetParamFnIDs",
                                  "InteractorSetParamFnIDs", "Pronouns", "ID_MOD"};
static_assert(sizeof kRoleNames / sizeof *kRoleNames == size_t(Role::Count), "role table");
static_assert(sizeof kRoleEnums / sizeof *kRoleEnums == size_t(Role::Count), "role table");

bool isIdStart(char c) { return std::isalpha(uint8_t(c)) || c == '_'; }
bool isId(char c) { return std::isalnum(uint8_t(c)) || c == '_'; }

bool readFile(const std::string &path, std::string &out, std::string *err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
} // namespace

const char *roleName(Role r) { return r < Role::Count ? kRoleNames[size_t(r)] : "?"; }
const char *defaultEnumForRole(Role r) { return r < Role::Count ? kRoleEnums[size_t(r)] : ""; }

// =============================================================================================
// Namespace

std::string Namespace::stripPrefix(std::string_view full) {
    // id_XXX_Name -> Name (the original's convention: chars [3..5] name the namespace).
    if (full.size() > 3 && full.substr(0, 3) == "id_") {
        size_t u = full.find('_', 3);
        if (u != std::string_view::npos && u + 1 < full.size()) return std::string(full.substr(u + 1));
    }
    return std::string(full);
}

int Namespace::add(const std::string &sym, int id) {
    auto it = byName_.find(sym);
    if (it != byName_.end()) return (id < 0 || it->second == id) ? it->second : -1;
    if (id < 0) id = int(names_.size());
    if (size_t(id) >= names_.size()) names_.resize(size_t(id) + 1);
    names_[size_t(id)] = sym;
    byName_.emplace(sym, id);
    byShort_.emplace(stripPrefix(sym), id); // first wins on a short-name clash
    return id;
}

int Namespace::id(std::string_view sym) const {
    auto it = byName_.find(sym);
    if (it != byName_.end()) return it->second;
    auto s = byShort_.find(sym);
    return s != byShort_.end() ? s->second : -1;
}

const char *Namespace::name(int id) const {
    if (id < 0 || size_t(id) >= names_.size() || names_[size_t(id)].empty()) return nullptr;
    return names_[size_t(id)].c_str();
}

std::string Namespace::shortName(int id) const {
    const char *n = name(id);
    return n ? stripPrefix(n) : std::string();
}

// =============================================================================================
// Vocabulary

Vocabulary::Vocabulary() {
    for (size_t i = 0; i < size_t(Role::Count); ++i) bind_.emplace_back(kRoleEnums[i]);
}

Namespace &Vocabulary::ns(const std::string &name) {
    auto it = byName_.find(name);
    if (it != byName_.end()) return *it->second;
    order_.push_back(std::make_unique<Namespace>(name));
    byName_[name] = order_.back().get();
    return *order_.back();
}

const Namespace *Vocabulary::find(const std::string &name) const {
    auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : it->second;
}

std::vector<std::string> Vocabulary::namespaceNames() const {
    std::vector<std::string> v;
    for (auto &n : order_) v.push_back(n->name());
    return v;
}

void Vocabulary::bind(Role r, const std::string &nsName) {
    if (r < Role::Count) bind_[size_t(r)] = nsName;
}

int Vocabulary::id(Role r, std::string_view sym) const {
    const Namespace *n = role(r);
    return n ? n->id(sym) : -1;
}

const char *Vocabulary::name(Role r, int id) const {
    const Namespace *n = role(r);
    return n ? n->name(id) : nullptr;
}

int Vocabulary::extend(Role r, const std::string &sym) { return roleNs(r).append(sym); }

bool Vocabulary::mergeInto(Role r, const std::string &from) {
    const Namespace *src = find(from);
    if (!src) return false;
    Namespace &dst = roleNs(r);
    if (&dst == src) return true;
    bool ok = true;
    for (const std::string &s : src->names())
        if (!s.empty() && dst.id(s) >= 0) ok = false;
    if (!ok) return false;
    for (const std::string &s : src->names())
        if (!s.empty()) dst.append(s);
    return true;
}

// enum-header parser. Original: FUN_0048a240 / tokenizer FUN_00489940 (docs/language.md §3.3).
bool Vocabulary::loadEnumHeader(std::string_view t, std::string *err) {
    // 1. strip comments (keep line structure irrelevant: only tokens matter)
    std::string s;
    s.reserve(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '/' && i + 1 < t.size() && t[i + 1] == '/') {
            while (i < t.size() && t[i] != '\n') ++i;
            s.push_back('\n');
        } else if (t[i] == '/' && i + 1 < t.size() && t[i + 1] == '*') {
            i += 2;
            while (i + 1 < t.size() && !(t[i] == '*' && t[i + 1] == '/')) ++i;
            ++i;
            s.push_back(' ');
        } else {
            s.push_back(t[i]);
        }
    }
    auto fail = [&](const std::string &m) { if (err) *err = m; return false; };
    // 2. walk: outside braces remember the last identifier (the enum name); inside, read entries.
    std::string lastId;
    int anon = 0;
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (c == '#') { // preprocessor line
            while (i < s.size() && s[i] != '\n') ++i;
            continue;
        }
        if (isIdStart(c)) {
            size_t b = i;
            while (i < s.size() && isId(s[i])) ++i;
            std::string w = s.substr(b, i - b);
            if (w != "enum" && w != "typedef") lastId = w;
            continue;
        }
        if (c != '{') { if (c == ';') lastId.clear(); ++i; continue; }
        ++i;
        std::string enumName = lastId.empty() ? "enum#" + std::to_string(anon++) : lastId;
        lastId.clear();
        Namespace &n = ns(enumName);
        int next = n.size();
        while (true) {
            while (i < s.size() && (std::isspace(uint8_t(s[i])) || s[i] == ',')) ++i;
            if (i >= s.size()) return fail("unterminated enum " + enumName);
            if (s[i] == '}') { ++i; break; }
            if (!isIdStart(s[i])) return fail("unexpected '" + std::string(1, s[i]) + "' in enum " + enumName);
            size_t b = i;
            while (i < s.size() && isId(s[i])) ++i;
            std::string sym = s.substr(b, i - b);
            while (i < s.size() && std::isspace(uint8_t(s[i]))) ++i;
            if (i < s.size() && s[i] == '=') { // libadlib extension
                ++i;
                while (i < s.size() && std::isspace(uint8_t(s[i]))) ++i;
                char *end = nullptr;
                long v = std::strtol(s.c_str() + i, &end, 0);
                if (end == s.c_str() + i) return fail("bad value for " + sym + " in enum " + enumName);
                i = size_t(end - s.c_str());
                next = int(v);
            }
            if (next < 0) return fail("negative value for " + sym);
            if (n.add(sym, next) < 0) return fail("duplicate symbol " + sym + " in enum " + enumName);
            ++next;
        }
    }
    return true;
}

bool Vocabulary::loadEnumHeaderFile(const std::string &path, std::string *err) {
    std::string text;
    return readFile(path, text, err) && loadEnumHeader(text, err);
}

void Vocabulary::loadList(const std::string &nsName, std::string_view text, const ListOptions &opt) {
    Namespace &n = ns(nsName);
    size_t line = 0, p = 0;
    while (p <= text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string_view::npos) e = text.size();
        std::string_view l = text.substr(p, e - p);
        p = e + 1;
        if (line++ < opt.skipLines) { if (e == text.size()) break; continue; }
        size_t b = 0;
        while (b < l.size() && std::isspace(uint8_t(l[b]))) ++b;
        size_t q = b;
        while (q < l.size() && !std::isspace(uint8_t(l[q]))) ++q;
        std::string_view tok = l.substr(b, q - b);
        if (!tok.empty() && !(!opt.comment.empty() && tok.substr(0, opt.comment.size()) == opt.comment) &&
            (opt.requirePrefix.empty() || tok.substr(0, opt.requirePrefix.size()) == opt.requirePrefix))
            n.append(std::string(tok));
        if (e == text.size()) break;
    }
}

bool Vocabulary::loadListFile(const std::string &nsName, const std::string &path, const ListOptions &opt,
                              std::string *err) {
    std::string text;
    if (!readFile(path, text, err)) return false;
    loadList(nsName, text, opt);
    return true;
}

} // namespace adlib
