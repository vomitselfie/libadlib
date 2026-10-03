// --strict: warnings for input the 1996 compiler accepted silently but that is almost certainly
// a mistake (dropped words, counts that build a different tree than the layout suggests, symbols
// from the wrong enum, sizes near the original's buffer limits). Output is never changed.
//
// The tree checks mirror how the game reads the PBN (PlanNode_ReadTree 0x45b000): a node is
// 0xABCDEFFF id count children..., a string is 0xABCDEFFE chars 0, anything else is a leaf.
#include <algorithm>
#include <map>

#include "adlib/pbn.h"
#include "adlib/vocabulary.h"
#include "internal.h"
#include "machine.h"

namespace adlib::cc {

namespace {

const char *kwName(uint32_t id) {
    for (const CompilerKeyword &k : compilerKeywords())
        if (k.id == id) return k.name;
    return "?";
}

struct Item {
    enum Kind { Leaf, String, Node } kind = Leaf;
    size_t word = 0;          // first word
    uint32_t id = 0;          // Node
    uint32_t rawCount = 0;    // Node
    size_t countWord = 0;     // Node: index of the count word
    std::vector<Item> kids;   // Node
};

class Analyzer {
public:
    Analyzer(std::string_view src, const CompilerSymbols &syms, const std::vector<TokenRec> &recs,
             const std::vector<uint32_t> &words, std::vector<Diagnostic> &out)
        : src_(src), syms_(syms), recs_(recs), w_(words), out_(out) {
        wordRec_.assign(words.size(), -1);
        for (size_t i = 0; i < recs.size(); ++i)
            for (size_t w = recs[i].w0; w < recs[i].w1 && w < words.size(); ++w) wordRec_[w] = int(i);
    }

    void warn(const std::string &code, const std::string &msg, int32_t offset) {
        Diagnostic d;
        d.severity = Diagnostic::Severity::Warning;
        d.code = code;
        d.message = msg;
        d.tokenOffset = offset;
        if (offset >= 0) {
            size_t lim = std::min<size_t>(size_t(offset), src_.size());
            d.line = int32_t(std::count(src_.begin(), src_.begin() + long(lim), '\n') + 1);
            d.offset = offset;
        }
        out_.push_back(d);
    }
    const TokenRec *recOf(size_t word) const {
        return word < wordRec_.size() && wordRec_[word] >= 0 ? &recs_[size_t(wordRec_[word])] : nullptr;
    }
    int32_t offOf(size_t word) const { const TokenRec *r = recOf(word); return r ? r->start : -1; }

    void tokens() {
        for (const TokenRec &r : recs_) {
            if (r.kind == TokenRec::Dropped) {
                std::string kw;
                for (const CompilerKeyword &k : compilerKeywords())
                    if (msvcStricmp(k.name, r.text.c_str()) == 0) kw = k.name;
                std::string lower = r.text;
                if (!kw.empty()) {
                    warn("keyword-case", "'" + r.text + "' is not a keyword (keywords are case-sensitive: " + kw +
                         "); the word is dropped but still shifts the positional counters", r.start);
                } else if (msvcStricmp(r.text.substr(0, 2).c_str(), "id") == 0 ||
                           msvcStricmp(r.text.substr(0, 2).c_str(), "a_") == 0) {
                    warn("dropped-word", "'" + r.text + "' is dropped: the identifier prefixes id, _, A_, AA_ are "
                         "case-sensitive", r.start);
                } else {
                    warn("dropped-word", "unknown word '" + r.text + "' is dropped (emits nothing but shifts the "
                         "positional counters)", r.start);
                }
            } else if (r.kind == TokenRec::Brace) {
                warn("dropped-word", "'" + r.text + "' has no meaning to the compiler and is dropped", r.start);
            }
            if (r.kind == TokenRec::String && r.text.size() >= 36)
                warn("long-string", "string token of " + std::to_string(r.text.size()) +
                     " bytes; the 1996 compiler crashes on string tokens of 40 bytes or more", r.start);
            if (r.kind != TokenRec::String && r.text.size() >= 36 && !(r.text.size() >= 40 && r.text.size() < 44 && adlib1996_))
                warn("long-token", "token of " + std::to_string(r.text.size()) +
                     " bytes; the 1996 compiler's token buffers are 40 bytes (tokens of 44+ bytes crash it)", r.start);
            if (r.kind == TokenRec::Number && r.text.size() > 1 && r.text[0] == '0' &&
                (r.text[1] == 'x' || r.text[1] == 'X') && r.text.find_first_of(".+-_") != std::string::npos)
                warn("number", "'" + r.text + "' mixes hex with other characters; only the leading hex digits count",
                     r.start);
            else if (r.kind == TokenRec::Number && r.text.find_first_not_of("0123456789+-.xX") != std::string::npos &&
                     !(r.text.size() > 1 && (r.text[1] == 'x' || r.text[1] == 'X')))
                warn("number", "'" + r.text + "' is read as " + numberDesc(r) + " (trailing characters ignored)",
                     r.start);
        }
    }
    std::string numberDesc(const TokenRec &r) const {
        if (r.w0 < w_.size()) return "0x" + hex(w_[r.w0]);
        return "?";
    }
    static std::string hex(uint32_t v) {
        char b[16];
        snprintf(b, sizeof b, "%X", v);
        return b;
    }

    // ---- tree ----
    bool parseItem(size_t &i, Item &it, int depth) {
        if (i >= w_.size()) return false;
        it.word = i;
        if (w_[i] == kTokNode) {
            it.kind = Item::Node;
            if (i + 1 >= w_.size()) { ++i; return false; }
            it.id = w_[i + 1];
            i += 2;
            if (it.id == END_PLAN) return true;
            if (i >= w_.size()) return false;
            it.countWord = i;
            it.rawCount = w_[i++];
            uint32_t n = it.rawCount & 0x7FFF;
            for (uint32_t k = 0; k < n; ++k) {
                if (i >= w_.size()) {
                    warn("count", std::string(kwName(it.id)) + " count " + std::to_string(n) +
                         " runs past the end of the plan", offOf(it.word));
                    return false;
                }
                Item c;
                if (depth > 200) return false;
                if (!parseItem(i, c, depth + 1)) { it.kids.push_back(std::move(c)); return false; }
                it.kids.push_back(std::move(c));
            }
            return true;
        }
        if (w_[i] == kTokString) {
            it.kind = Item::String;
            while (i < w_.size() && w_[i] != 0) ++i;
            ++i;
            return true;
        }
        it.kind = Item::Leaf;
        ++i;
        return true;
    }

    std::string groupOf(const Item &c) const {
        if (c.kind != Item::Leaf) return "";
        const TokenRec *r = recOf(c.word);
        return r && r->kind == TokenRec::Identifier && !r->special ? r->group : "";
    }
    void slot(const Item &n, size_t k, Role role, const char *what) {
        if (k >= n.kids.size()) return;
        const Item &c = n.kids[k];
        std::string need = syms_.roleGroup(int(role));
        const TokenRec *r = recOf(c.word);
        if (c.kind != Item::Leaf) return; // lintPlan reports the shape
        std::string g = groupOf(c);
        if (g.empty() || need.empty() || g == need) return;
        if (g == syms_.roleGroup(int(Role::Pronoun))) return; // pronouns are resolved at run time
        warn("namespace", std::string(kwName(n.id)) + ": " + what + " slot holds " + (r ? r->text : "?") +
             " from " + g + " (expected " + need + "); it compiles to that enum's number", offOf(c.word));
    }
    void checkCount(const Item &n) {
        const TokenRec *r = recOf(n.countWord);
        if (!r) return;
        bool plain = r->kind == TokenRec::Number && r->text.find('.') == std::string::npos;
        if (r->kind == TokenRec::Identifier && r->special && r->text.size() >= 3 && r->text[1] >= '0' && r->text[1] <= '9')
            plain = true; // _N_
        if (!plain)
            warn("count", std::string("the count of ") + kwName(n.id) + " is '" + r->text +
                 "', not an integer literal (it compiles to 0x" + hex(n.rawCount) + ")", r->start);
        else if (n.rawCount & 0xFFFF0000u)
            warn("count", std::string("the count of ") + kwName(n.id) + " is " + std::to_string(int32_t(n.rawCount)) +
                 ": the game reads only the low 15 bits", r->start);
    }

    // Token-aware tree checks. Arities, interactor options and action lists are tools::lintPlan's
    // (adlibc runs it on the output); these need the source tokens behind each dword.
    void check(const Item &n) {
        if (n.kind != Item::Node) return;
        if (n.id != END_PLAN) checkCount(n);
        for (const Item &c : n.kids)
            if (c.kind == Item::Node && c.id == END_PLAN)
                warn("count", std::string("END_PLAN is read as a child of ") + kwName(n.id) + ": a count is too large",
                     offOf(c.word));
        switch (n.id) {
        case DECLARE_BEHAVIOR: slot(n, 0, Role::Behavior, "behaviour type"); break;
        case SET_MESSAGE_INTERACTOR: case SET_LOC_MESSAGE_INTERACTOR: slot(n, 0, Role::Message, "message id"); break;
        case SET_COLLISION_INTERACTOR: case SET_LOC_COLLISION_INTERACTOR:
            slot(n, 0, Role::CollisionObject, "collision object");
            slot(n, 1, Role::CollisionObject, "collision object");
            break;
        case DO_ACTION: case DO_LOCAL_ACTION: case DO_NET_ACTION: slot(n, 0, Role::Action, "action id"); break;
        case DECIDE_BY_AMONG: case DECIDE_BY_WITH_AMONG: slot(n, 0, Role::Decision, "decision id"); break;
        case CHANGE_TO_BEHAVIOR:
            if (!n.kids.empty() && n.kids[0].kind == Item::String)
                warn("beh-ref", "CHANGE_TO_BEHAVIOR's behaviour name was emitted as a string, not resolved: "
                     "the name must be the second token after the keyword", offOf(n.kids[0].word));
            break;
        case CHANGE_OF_PLAN:
            if (n.kids.size() > 1 && n.kids[1].kind == Item::String)
                warn("beh-ref", "CHANGE_OF_PLAN's behaviour name was emitted as a string, not resolved: it must "
                     "be the third token after the keyword", offOf(n.kids[1].word));
            break;
        case SET_BEHAVIOR_PARAM: slot(n, 0, Role::BehaviorParam, "behaviour parameter id"); break;
        case SET_INTERACTOR_PARAM: slot(n, 0, Role::InteractorParam, "interactor parameter id"); break;
        case ADD_AGENDA_ITEM: case REMOVE_AGENDA_ITEM: slot(n, 0, Role::Agenda, "agenda item id"); break;
        case SET_TIMEOUTMSG: slot(n, 1, Role::Message, "message id"); break;
        default: break;
        }
        for (const Item &c : n.kids) check(c);
    }

    void tree() {
        size_t i = 0;
        bool ended = false;
        while (i < w_.size()) {
            Item it;
            size_t at = i;
            bool ok = parseItem(i, it, 0);
            if (it.kind == Item::Node) {
                if (it.id == END_PLAN) { ended = true; break; }
                check(it);
            } else {
                const TokenRec *r = recOf(at);
                warn("structure", "stray " + std::string(it.kind == Item::String ? "string" : "value") +
                     (r ? " '" + r->text + "'" : std::string()) + " at the top level: a count is too small",
                     offOf(at));
            }
            if (!ok) break;
        }
        if (!ended) {
            bool any = std::find(w_.begin(), w_.end(), 0xFFFF0000u) != w_.end();
            warn("end-plan", any ? "END_PLAN is consumed inside a statement (a count is too large)"
                                 : "missing END_PLAN", -1);
        }
    }

private:
    std::string_view src_;
    const CompilerSymbols &syms_;
    const std::vector<TokenRec> &recs_;
    const std::vector<uint32_t> &w_;
    std::vector<Diagnostic> &out_;
    std::vector<int> wordRec_;
public:
    bool adlib1996_ = false;
};

} // namespace

void strictAnalysis(std::string_view source, const CompilerSymbols &syms, const std::vector<TokenRec> &recs,
                    const std::vector<uint32_t> &words, const PrescanFacts &pre, bool adlib1996,
                    std::vector<Diagnostic> &out) {
    Analyzer a(source, syms, recs, words, out);
    a.adlib1996_ = adlib1996;
    for (const auto &f : pre.facts)
        if (!(adlib1996 && (f.code == "name-overflow" || f.code == "table-overflow"))) // already reported
            a.warn(f.code, f.message, f.offset);
    a.tokens();
    a.tree();
    // Source-level hazards.
    for (size_t i = 0; i < source.size(); ++i)
        if (source[i] == '\r' && (i + 1 >= source.size() || source[i + 1] != '\n')) {
            a.warn("bare-cr", "bare CR (not followed by LF): the original skips the rest of the next line", int32_t(i));
            break;
        }
    size_t lastNl = source.find_last_of('\n');
    std::string_view tail = lastNl == std::string_view::npos ? source : source.substr(lastNl + 1);
    if (!tail.empty()) {
        bool comment = tail.find("//") != std::string_view::npos || tail.find("/*") != std::string_view::npos ||
                       tail.find('(') != std::string_view::npos || tail.find('#') != std::string_view::npos;
        if (comment)
            a.warn("eof-comment", "the file ends in a comment without a newline: the original reads past the end "
                   "of the source looking for one", int32_t(source.size()));
    }
    std::stable_sort(out.begin(), out.end(), [](const Diagnostic &x, const Diagnostic &y) {
        if (x.severity != y.severity) return x.severity < y.severity;
        return x.tokenOffset < y.tokenOffset;
    });
}

} // namespace adlib::cc
