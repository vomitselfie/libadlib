// adlib-tools: PBN -> ADLIB compiler source (port of the reconstruction's Python decompiler).
//
// The original compiler is a flat token translator (docs/research/grammar.md §1): every keyword is
// followed by the author's child count, and each token emits its dwords independently. So the
// decompiler walks the raw token stream (not the loaded tree) and prints one token per dword
// group, choosing a spelling the compiler maps back to exactly the same dwords:
//   - symbols only when the compiler's case-insensitive first-match lookup gives the same value;
//   - ints for |v| < 0x100000, otherwise the shortest round-tripping float with '.', else hex;
//   - behaviour references by name only when the name is the first one with that index.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "adlib/tools.h"

namespace adlib::tools {

namespace {

const char *specialName(uint32_t v) {
    switch (v) {
    case kBroadcast: return "_BROADCAST_";
    case kStraightAhead: return "_STRAIGHTAHEAD_";
    case kTemporarily: return "_TEMPORARILY_";
    case kMessageData: return "_MESSAGE_DATA_";
    case kNoBehChange: return "_NO_BEH_CHANGE_";
    case kPreviousBeh: return "_PREVIOUS_BEH_";
    default: return nullptr;
    }
}

std::string hex8(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}

// Can `s` be written as a compiler string token? ('_' is how a space is written; the tokenizer
// splits on separators and comment starts; length < 40 with quotes.)
bool expressible(const std::string &s, std::string *why) {
    if (s.size() > 37) { if (why) *why = "longer than 37 characters"; return false; }
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = uint8_t(s[i]);
        if (c == '_' || c == '"') { if (why) *why = "contains '_' or '\"'"; return false; }
        if (c < 32 && c != ' ') { if (why) *why = "contains a control character"; return false; }
        if (c == ',' || c == '&' || c == '\t' || c == '(' || c == '#' || c == '}' ||
            (c == '/' && i + 1 < s.size() && (s[i + 1] == '/' || s[i + 1] == '*'))) {
            if (why) *why = "contains a separator or comment start";
            return false;
        }
    }
    return true;
}

std::string quote(const std::string &s) {
    std::string r = "\"";
    for (char c : s) r += c == ' ' ? '_' : c;
    return r + "\"";
}

struct Decompiler {
    const std::vector<uint32_t> &w;
    const SymbolTable &syms;
    const DecompileOptions &opt;
    std::vector<std::string> behs;
    std::vector<std::string> lines;
    size_t pos = 0;
    std::string copPlan; // last CHANGE_OF_PLAN target (upper case)
    int behDecl = 0;

    uint32_t at(size_t p) const {
        if (p >= w.size()) throw std::runtime_error("truncated token stream at dword " + std::to_string(p));
        return w[p];
    }

    std::string readStr() {
        ++pos;
        std::string s;
        while (at(pos)) s += char(at(pos++) & 0xFF);
        ++pos;
        return s;
    }

    std::string strLit(const std::string &s) {
        std::string why;
        if (!expressible(s, &why))
            throw std::runtime_error("string \"" + s + "\" is not expressible in compiler source: " + why);
        return quote(s);
    }

    static bool firstIndex(const std::vector<std::string> &v, uint32_t i) {
        if (i >= v.size()) return false;
        for (size_t k = 0; k < i; ++k)
            if (v[k] == v[i]) return false;
        std::string raw = v[i];
        for (char &c : raw)
            if (c == '_') c = ' ';
        return expressible(raw, nullptr);
    }

    std::string actionShort(int acf) const {
        const char *n = syms.vocab().name(Role::Action, acf);
        return n ? Namespace::stripPrefix(n) : std::string();
    }

    std::string leaf(uint32_t v, uint32_t parent, size_t idx, int64_t acf, std::string *note) {
        if (parent == CHANGE_TO_BEHAVIOR && idx == 0 && !specialName(v)) {
            if (firstIndex(behs, v)) return "\"" + behs[v] + "\"";
            if (note && v < behs.size()) *note = "behaviour \"" + behs[v] + "\"";
            return std::to_string(v);
        }
        if (parent == CHANGE_OF_PLAN && idx == 1 && opt.planBehaviours) {
            auto it = opt.planBehaviours->find(copPlan);
            if (it != opt.planBehaviours->end() && firstIndex(it->second, v)) return "\"" + it->second[v] + "\"";
        }
        if (const char *s = specialName(v)) return s;
        uint16_t hi = uint16_t(v >> 16);
        if (hi == kTagPronoun) {
            const char *n = syms.vocab().name(Role::Pronoun, int(v & 0xFFFF));
            if (n && SymbolTable::isPronounName(n)) {
                const SymbolTable::Entry *e = syms.lookup(n);
                if (e && e->value == int(v & 0xFFFF)) return n;
            }
            return hex8(v);
        }
        if (hi == kTagLocalReal || hi == kTagNameRef) return hex8(v); // exact; names unknown
        int32_t s = int32_t(v);
        if (!(-0x100000 < s && s < 0x100000)) return formatFloatLiteral(v);
        std::string n;
        if (idx == 0) {
            switch (nodeClass(parent)) {
            case 0x00180000: // DO_*, SET_INTERACTOR_PARAM
                n = parent == SET_INTERACTOR_PARAM ? syms.roleName(Role::InteractorParam, s) : syms.roleName(Role::Action, s);
                break;
            case 0x00170000: n = syms.roleName(Role::Decision, s); break;
            case 0x00050000: n = syms.roleName(Role::Message, s); break;
            case 0x00030000: n = syms.roleName(Role::CollisionObject, s); break;
            case 0x001A0000:
                if (parent != SET_TIMEOUTMSG) n = syms.roleName(Role::Agenda, s);
                break;
            default:
                if (parent == SET_BEHAVIOR_PARAM) n = syms.roleName(Role::BehaviorParam, s);
                else if (parent == DECLARE_BEHAVIOR) n = syms.roleName(Role::Behavior, s);
            }
        } else if (idx == 1 && nodeClass(parent) == 0x00030000) {
            n = syms.roleName(Role::CollisionObject, s);
        } else if (idx == 1 && parent == SET_TIMEOUTMSG) {
            n = syms.roleName(Role::Message, s);
        } else if (nodeClass(parent) == 0x00180000 && parent != SET_INTERACTOR_PARAM && acf >= 0) {
            auto it = syms.argHints.find({actionShort(int(acf)), int(idx)});
            if (it != syms.argHints.end()) n = syms.nameIn(it->second, s);
        }
        return n.empty() ? std::to_string(s) : n;
    }

    void node(int depth, uint32_t parent, size_t idx, int64_t acf) {
        std::string ind;
        for (int i = 0; i < depth; ++i) ind += opt.indent;
        size_t start = pos;
        uint32_t t = at(pos);
        if (t == kTokNode) {
            uint32_t id = at(pos + 1);
            pos += 2;
            if (id == END_PLAN) { lines.push_back(ind + "END_PLAN"); return; }
            const char *kw = nodeIdName(id);
            if (!kw) throw std::runtime_error("unknown node id " + hex8(id) + " at dword " + std::to_string(start));
            uint32_t cnt = at(pos++);
            uint32_t n = cnt & 0x7FFF;
            int64_t myAcf = nodeClass(id) == 0x00180000 && id != SET_INTERACTOR_PARAM ? int64_t(int32_t(at(pos))) : -1;
            if (depth == 0 && id == DECLARE_BEHAVIOR && !lines.empty() && !lines.back().empty()) lines.push_back("");
            std::string line = ind + kw + " " + std::to_string(cnt);
            std::vector<std::string> notes;
            uint32_t k = 0;
            for (; k < n && at(pos) != kTokNode; ++k) {
                if (at(pos) == kTokString) {
                    std::string s = readStr();
                    line += " " + strLit(s);
                    if (id == CHANGE_OF_PLAN && k == 0) {
                        copPlan = s;
                        for (char &c : copPlan) c = char(std::toupper(uint8_t(c)));
                    }
                } else {
                    std::string note;
                    line += " " + leaf(at(pos), id, k, myAcf, opt.annotate ? &note : nullptr);
                    if (!note.empty()) notes.push_back(note);
                    ++pos;
                }
            }
            if (opt.annotate) {
                if (id == DECLARE_BEHAVIOR) notes.insert(notes.begin(), "#" + std::to_string(behDecl++));
                if (cnt & 0x8000) notes.push_back("count has bit 15 set");
                if (opt.annotateOffsets) notes.push_back("@" + std::to_string(start + 1)); // dword offset incl. header
                std::string c;
                for (auto &s : notes) c += (c.empty() ? "" : "; ") + s;
                if (!c.empty()) line += "  // " + c;
            } else if (id == DECLARE_BEHAVIOR) {
                behDecl++;
            }
            lines.push_back(line);
            for (uint32_t i = k; i < n; ++i) {
                size_t first = lines.size();
                node(depth + 1, id, i, myAcf);
                // decision branch value = child index - 1 (DECIDE_BY_WITH_AMONG: child 1 is the DataBlock)
                if (opt.annotate && (id == DECIDE_BY_AMONG || id == DECIDE_BY_WITH_AMONG) && i >= (id == DECIDE_BY_AMONG ? 1u : 2u) &&
                    first < lines.size())
                    lines[first] += (lines[first].find("  // ") == std::string::npos ? "  // " : "; ") + std::string("=") + std::to_string(i - 1);
            }
            return;
        }
        if (t == kTokString) {
            lines.push_back(ind + strLit(readStr()));
            return;
        }
        ++pos;
        lines.push_back(ind + leaf(t, parent, idx, acf, nullptr));
    }
};

} // namespace

std::string formatFloatLiteral(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    if (std::isfinite(f)) {
        for (int p = 1; p < 12; ++p) {
            char b[64];
            std::snprintf(b, sizeof b, "%.*g", p, double(f));
            std::string s = b;
            if (s.find_first_of("eEnN") != std::string::npos) continue;
            float g = float(std::strtod(b, nullptr));
            uint32_t gb;
            std::memcpy(&gb, &g, 4);
            if (gb == bits) return s.find('.') != std::string::npos ? s : s + ".0";
        }
    }
    return hex8(bits);
}

std::vector<std::string> behaviourNames(const std::vector<uint32_t> &w) {
    std::vector<std::string> out;
    for (size_t i = 0; i + 4 < w.size(); ++i) {
        if (w[i] == kTokNode && w[i + 1] == DECLARE_BEHAVIOR && w[i + 4] == kTokString) {
            std::string nm;
            for (size_t j = i + 5; j < w.size() && w[j]; ++j) nm += char(w[j] & 0xFF);
            for (char &c : nm)
                if (c == ' ') c = '_';
            out.push_back(nm);
        }
    }
    return out;
}

bool decompile(const std::vector<uint32_t> &words, const SymbolTable &syms, const DecompileOptions &opt,
               std::string &out, std::string *err) {
    Decompiler d{words, syms, opt, behaviourNames(words), {}, 0, {}, 0};
    try {
        while (d.pos < words.size()) d.node(0, 0, 0, -1);
    } catch (const std::exception &e) {
        if (err) *err = e.what();
        return false;
    }
    if (err) err->clear();
    const char *nl = opt.crlf ? "\r\n" : "\n";
    out.clear();
    if (opt.header) {
        std::string n = opt.planName.empty() ? "plan" : opt.planName;
        out += "// " + n + ".txt -- ADLIB compiler source decompiled from " + n + ".PBN by `adlib decompile`." + nl;
        out += "// Layout and symbol spelling are reconstructions; the token stream recompiles" + std::string(nl);
        out += "// byte-identically (docs/tools.md)." + std::string(nl) + nl;
    }
    for (auto &l : d.lines) out += l + nl;
    if (out.size() > kMaxCompilerSource && err)
        *err = "warning: " + std::to_string(out.size()) + " bytes exceeds the original compiler's source limit (" +
               std::to_string(kMaxCompilerSource) + "); drop --annotate/--crlf or split the plan";
    return true;
}

} // namespace adlib::tools
