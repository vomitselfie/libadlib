// CPlanCompiler_Compile (0x488880): PrescanDecls (0x488900), then EmitLeaf (0x489020) once per
// token, then WritePbn (0x488260), emulated over the byte-level machine of machine.h.
//
// Two compatibility modes (docs/adlibc.md):
//   Compat::Adlib1996   the 1996 machine exactly: fixed-size name tables in one object, 40-byte
//                       token buffers, stale stack bytes. Input on which it crashes, writes past
//                       its object or reads uninitialised memory is an error; input it accepts but
//                       mangles (table overflow, stale string bytes) is reproduced with a warning.
//   Compat::Adlib       the same language with the 1996 implementation limits lifted (unbounded
//                       tables, tokens and output; no stale-memory reads). Identical output for
//                       every input that stays within those limits.
#include <algorithm>
#include <unordered_map>

#include "internal.h"
#include "machine.h"

namespace adlib {

using namespace cc;

const Diagnostic *CompileResult::error() const {
    for (const Diagnostic &d : diagnostics)
        if (d.severity == Diagnostic::Severity::Error) return &d;
    return nullptr;
}

std::string Diagnostic::format(const std::string &file, bool stripMessage) const {
    std::string msg = message;
    if (stripMessage) { // like Python's str.strip() in the oracle CLI
        const char *ws = " \t\n\r\v\f";
        size_t b = msg.find_first_not_of(ws), e = msg.find_last_not_of(ws);
        msg = b == std::string::npos ? std::string() : msg.substr(b, e - b + 1);
    }
    std::string loc = line ? file + ":" + std::to_string(*line) : file;
    if (severity == Severity::Error) {
        std::string s = loc + ": error [" + stage + "/" + kind + "]";
        if (offset) s += " (byte " + std::to_string(*offset) + ")";
        return s + ": " + msg;
    }
    std::string s = loc + (severity == Severity::Warning ? ": warning: " : ": note: ") + msg;
    if (!code.empty()) s += " [" + code + "]";
    return s;
}

namespace {

// Positional counters DAT_0051803c..4c (16-bit, initial value 100, process-global; the oracle and
// adlibc start every compile from the .data image).
struct Counters { uint16_t beh = 100, ctb = 100, cop = 100, sip = 100, dlr = 100; };

bool isIdentifier(const std::string &b) { // FUN_00488f30 (reads the NUL after a short token)
    auto c = [&](size_t i) { return i < b.size() ? b[i] : '\0'; };
    return (c(0) == 'i' && c(1) == 'd') || c(0) == '_' ||
           (c(0) == 'A' && (c(1) == '_' || (c(1) == 'A' && c(2) == '_')));
}
bool isNumber(const std::string &b) { // FUN_00488fc0
    char c = b.empty() ? '\0' : b[0];
    return (c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-';
}
std::string lowerAscii(const std::string &s) {
    std::string r = s;
    for (char &c : r) if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return r;
}

// Strip the first and last character of the token buffer in place (FUN_0048a900).
std::string stripInPlace(TokBuf &tb, std::string &buf) {
    size_t L = buf.size();
    if (L >= 1) tb.put(int32_t(L - 1), 0);
    std::string s = L >= 2 ? buf.substr(1, L - 2) : std::string();
    if (L >= 1) buf.resize(L - 1);
    return s;
}

// The behaviour / local-real / interactor name tables. 1996: char[60][32], char[60][32] and
// char[150][32] in the object, filled by strcpy (long names spill into the next slot, extra
// entries land in the next table). Adlib: unbounded, exact.
struct Tables {
    Machine &m;
    bool hb;
    std::vector<std::string> beh, loc, ints;

    void set(std::vector<std::string> &v, int32_t base, int32_t i, const std::string &s, const char *what) {
        if (hb) { m.strcpyTo(base + i * 32, s.c_str(), what); return; }
        if (i < 0) return;
        if (size_t(i) >= v.size()) v.resize(size_t(i) + 1);
        v[size_t(i)] = s;
    }
    void setBeh(int32_t k, const std::string &s) { set(beh, kBehNames, k, s, "behaviour name table"); }
    void setLocal(int32_t i, const std::string &s) { set(loc, kLocalNames, i, s, "local-real name table"); }
    void setInt(int32_t k, const std::string &s) { set(ints, kIntNames, k, s, "interactor name table"); }
    void msgInteractor(int32_t nb, int32_t col, const std::string &tok) { // dev-dialog list only
        if (hb) m.strcpyTo(m.msgBase + (nb * 20 + col) * 32, tok.c_str(),
                           "message-interactor table (61 rows: at most 60 behaviours)");
    }
    // FindLocalReal 0x489c20 / FindInteractorName 0x489d30: first slot equal to s (an empty slot
    // matches ""), or -1.
    int find(const std::vector<std::string> &v, int32_t base, int n, const std::string &s) const {
        if (hb) {
            for (int k = 0; k < n; ++k)
                if (m.cstrAt(base + k * 32) == s) return k;
            return -1;
        }
        for (size_t k = 0; k < v.size(); ++k)
            if (v[k] == s) return int(k);
        // "" matches the first free slot, which exists only while the 1996 table is not full
        return s.empty() && int(v.size()) < n ? int(v.size()) : -1;
    }
    int findLocal(const std::string &s) const { return find(loc, kLocalNames, 60, s); }
    int findInt(const std::string &s) const { return find(ints, kIntNames, 150, s); }
    // 0x48a030: slot 0, then while slot k is non-empty compare slot k+1.
    int16_t findBehavior(const std::string &name) const {
        if (hb) {
            if (m.cstrAt(kBehNames) == name) return 0;
            int16_t k = 0;
            while (m.mem[size_t(kBehNames + k * 32)] != 0) {
                ++k;
                if (kBehNames + k * 32 >= m.objEnd)
                    throw Hazard{"corrupt", "behaviour lookup runs past the original's object"};
                if (m.cstrAt(kBehNames + k * 32) == name) return k;
            }
        } else {
            for (size_t k = 0; k < beh.size(); ++k)
                if (beh[k] == name) return int16_t(k);
            if (name.empty()) return int16_t(beh.size());
        }
        throw Thrown{std::string(kMsgNoBehavior) + name};
    }
};

struct Prescan {
    Machine &m;
    Tables &t;
    PrescanFacts *facts; // main plan only
    uint8_t pf[0x38] = {};
    uint8_t pfWritten[0x38] = {}; // bytes of the frame this compile wrote (the rest: earlier code's)
    int16_t nb = 0, col = 0, nint = 0;

    // Frame [E-0x38, E): saved EBX, local_34 (behaviours), local_32 (column), local_30
    // (interactor names), local_2c[40] (token), local_4 (local-real count).
    void run() {
        uint32_t init = 0xFFFFFFFF; // E-4 holds OpenPlanSource's try level (-1) (the dialog calls
        std::memcpy(pf + 52, &init, 4); // OpenPlanSource and Compile from the same frame)
        TokBuf tb = t.hb ? TokBuf{pf, 12, 44, 44} : TokBuf{pf, 12, 40, -1};
        tb.written = pfWritten;
        for (int i = 4; i < 10; ++i) pfWritten[i] = 1; // local_34, local_32, low half of local_30
        std::string buf;
        auto next = [&]() { buf = m.nextToken(tb); return buf; };
        auto fact = [&](const std::string &code, const std::string &msg, bool hbOnly) {
            if (facts && (!hbOnly || t.hb)) facts->facts.push_back({code, msg, m.pos});
        };
        auto count16 = [&]() { int16_t v; std::memcpy(&v, pf + 52, 2); return v; };
        if (m.pos < m.size) {
            do {
                if (m.size < m.pos || m.cur() == 0) break;
                next();
                if (msvcStricmp(buf.c_str(), "DECLARE_LOCAL_REALS") == 0) {
                    if (buf != "DECLARE_LOCAL_REALS")
                        fact("keyword-case", "'" + buf + "' declares local reals in the prescan but is not a keyword for the code generator", false);
                    next();
                    if (!isIdentifier(buf) && !isNumber(buf)) throw Thrown{kMsgArgEnumLocals};
                    uint32_t v = 0;
                    bool undef = false;
                    if (msvcParsePlanNumber(buf, v, &undef)) {
                        if (undef && t.hb) throw Hazard{"undefined", "local-real count '" + buf + "': the original reads an uninitialised value"};
                        std::memcpy(pf + 52, &v, 4);
                    } else {
                        fact("locals-count", "local-real count '" + buf + "' is not a number: the prescan keeps the previous count (" +
                             std::to_string(count16()) + ")", false);
                    }
                    int16_t i = 0;
                    if (0 < count16()) {
                        do {
                            next();
                            if (tb.get(0) != '"') throw Thrown{kMsgLocalName};
                            std::string s = stripInPlace(tb, buf);
                            if (s.size() > 31) fact("name-overflow", "local-real name \"" + s + "\" is longer than 31 characters: the original spills it into the next table slot", true);
                            if (i == 60) fact("table-overflow", "more than 60 local reals: the original writes the rest over its interactor-name table", true);
                            t.setLocal(i, s);
                            ++i;
                        } while (i < count16());
                    }
                }
                if (msvcStricmp(buf.c_str(), "DECLARE_BEHAVIOR") == 0) {
                    if (buf != "DECLARE_BEHAVIOR")
                        fact("keyword-case", "'" + buf + "' declares a behaviour in the prescan but emits no node (keywords are case-sensitive)", false);
                    col = 0;
                    next();
                    if (!isIdentifier(buf) && !isNumber(buf)) throw Thrown{kMsgArgEnum};
                    next();
                    if (!isIdentifier(buf)) throw Thrown{kMsgBehType};
                    next();
                    if (tb.get(0) != '"') throw Thrown{kMsgBehName};
                    std::string s = stripInPlace(tb, buf);
                    if (s.size() > 31) fact("name-overflow", "behaviour name \"" + s + "\" is longer than 31 characters: the original spills it into the next table slot", true);
                    if (nb == 60) fact("table-overflow", "more than 60 behaviours: the original stores the 61st name over its local-real table", true);
                    t.setBeh(nb, s);
                    ++nb;
                }
                if (msvcStricmp(buf.c_str(), "SET_MESSAGE_INTERACTOR") == 0) {
                    next();
                    if (!isIdentifier(buf) && !isNumber(buf)) throw Thrown{kMsgArgEnum};
                    next();
                    if (!isIdentifier(buf) && !isNumber(buf)) throw Thrown{kMsgMsgType};
                    t.msgInteractor(nb, col, buf);
                    ++col;
                }
                if (msvcStricmp(buf.c_str(), "SET_INTERACTOR_PARAM") == 0) {
                    next();
                    if (!isIdentifier(buf) && !isNumber(buf)) throw Thrown{kMsgArgEnum};
                    next();
                    if (msvcStricmp(buf.c_str(), "id_SET_InteractorName") == 0) {
                        next();
                        if (tb.get(0) != '"') throw Thrown{kMsgBehName};
                        std::string s = stripInPlace(tb, buf);
                        if (s.size() > 31) fact("name-overflow", "interactor name \"" + s + "\" is longer than 31 characters: the original spills it into the next table slot", true);
                        if (nint == 150) fact("table-overflow", "more than 150 interactor names: the original writes the rest over its keyword table", true);
                        t.setInt(nint, s);
                        ++nint;
                    }
                }
            } while (m.pos < m.size);
        }
        std::memcpy(pf + 4, &nb, 2);
        std::memcpy(pf + 6, &col, 2);
        std::memcpy(pf + 8, &nint, 2);
        if (tb.retClobbered)
            throw Hazard{"crash", "a token of 44 or more bytes overwrites the prescan's return address; "
                                  "the original compiler would crash"};
    }
};

class Compiler {
public:
    Compiler(const CompilerSymbols &syms, const CompileOptions &opt, std::string_view source)
        : syms_(syms), opt_(opt), source_(source), hb_(opt.compat == Compat::Adlib1996),
          m_(syms.image().d.m), tables_{m_, hb_, {}, {}, {}} {
        m_.hb = hb_;
    }
    CompileResult run();

private:
    const CompilerSymbols &syms_;
    const CompileOptions &opt_;
    std::string_view source_;
    bool hb_;
    Machine m_;
    Tables tables_;
    Counters G;
    // EmitLeaf frame from E-0x38: local_38[40] (token), then the SEH record (link, handler, try
    // level), saved EBP, return address. Stale bytes in it are what an unterminated string reads.
    uint8_t ef_[60] = {};
    // Which of local_38's bytes this compile wrote. The others (saved EBX, the high half of
    // local_30, prescan buffer bytes no token reached) hold whatever ran before the compile (the
    // previous compile in the dev dialog); adlibc models them as zero, which is what a fresh
    // process has (the oracle with fresh_stack). The SEH record from byte 40 on is the current
    // fs:0 chain (0xFFFFFFFF in the oracle, a stack address in the game: no quote byte).
    uint8_t efWritten_[60] = {};
    uint32_t fs0_ = 0xFFFFFFFF;
    std::string planName_; // C string at acStack_88 (CHANGE_OF_PLAN's plan name, or stale)
    std::vector<TokenRec> recs_;
    PrescanFacts facts_;
    std::vector<Diagnostic> hbWarn_;
    std::unordered_map<std::string, int32_t> symIndex_; // lower-case name -> first entry offset
    std::unordered_map<std::string, int32_t> specIndex_;
    std::unordered_map<std::string, std::optional<std::string>> planCache_;

    void buildIndex() {
        symIndex_.clear();
        specIndex_.clear();
        size_t k = 0;
        for (int32_t off = kSymbols; off + kEntry <= int32_t(m_.mem.size()); off += kEntry, ++k) {
            if (m_.r32(off + 0x24) == 0xFFFFFFFF) break;
            bool mem = (hb_ && syms_.image().d.memoryNames) || k >= syms_.symbols().size();
            symIndex_.emplace(lowerAscii(mem ? m_.cstrAt(off) : syms_.symbols()[k].name), off);
        }
        for (int32_t off = kSpecials;; off += kEntry) {
            if (m_.r32(off + 0x24) == 0xFFFFFFFF) break;
            specIndex_.emplace(m_.cstrAt(off), off);
        }
    }
    // CPlanCompiler_LookupSymbol 0x489f00: returns the entry's offset.
    int32_t lookupSymbol(const std::string &t) {
        auto s = specIndex_.find(t);
        if (s != specIndex_.end()) return s->second;
        auto it = symIndex_.find(lowerAscii(t));
        if (it != symIndex_.end()) return it->second;
        return m_.symEnd - kEntry; // terminator
    }
    // FUN_00489e40: exact compare against the (possibly overwritten) keyword table.
    int32_t findKeyword(const std::string &t) {
        for (int32_t off = kKeywords;; off += kEntry) {
            if (m_.r32(off + 0x24) == 0xFFFFFFFF) return off;
            if (m_.cstrAt(off) == t) return off;
        }
    }
    uint32_t efR32(int i) const { uint32_t v; std::memcpy(&v, ef_ + i, 4); return v; }
    void hbWarning(const std::string &code, const std::string &msg, int32_t off) {
        Diagnostic d;
        d.severity = Diagnostic::Severity::Warning;
        d.code = code;
        d.message = msg;
        d.tokenOffset = off;
        d.offset = off;
        size_t lim = std::min<size_t>(size_t(std::max(off, 0)), source_.size());
        d.line = int32_t(std::count(source_.begin(), source_.begin() + long(lim), '\n') + 1);
        hbWarn_.push_back(d);
    }
    void emitWord(uint32_t v) { putWord(m_.count, v); ++m_.count; }
    void putWord(int32_t idx, uint32_t v) {
        if (hb_ && idx >= kOutCap)
            throw Hazard{"corrupt", "the plan emits more than 30000 dwords; the original writes past "
                                    "its 120000-byte output buffer"};
        if (size_t(idx) >= m_.out.size()) m_.out.resize(size_t(idx) + 64, 0);
        m_.out[size_t(idx)] = v;
    }
    // CPlanCompiler_EmitString 0x4898b0: scans the token buffer (and the stale bytes after the
    // token's NUL) for the closing quote within 41 characters.
    bool emitString(TokBuf &tb, const std::string &tok, TokenRec &rec) {
        if (!hb_) { // the string up to the closing quote inside the token
            size_t q = tok.find('"', 1);
            if (q == std::string::npos) return false;
            putWord(m_.count, 0xABCDEFFE);
            for (size_t i = 1; i < q; ++i) putWord(m_.count + int32_t(i), uint32_t(int32_t(int8_t(tok[i] == '_' ? ' ' : tok[i]))));
            putWord(m_.count + int32_t(q), 0);
            m_.count += int32_t(q) + 1;
            return true;
        }
        putWord(m_.count, 0xABCDEFFE);
        int16_t prev = 0;
        for (;;) {
            int16_t i = int16_t(prev + 1);
            if (i < 40 && !efWritten_[i]) rec.unknownRead = true; // modelled as 0
            char c = char(tb.get(i));
            if (c == '"') {
                putWord(m_.count + i, 0);
                m_.count += prev + 2;
                if (size_t(i) > tok.size()) rec.residue = true;
                return true;
            }
            if (c == '_') c = ' ';
            putWord(m_.count + i, uint32_t(int32_t(c)));
            prev = i;
            if (!(i < 0x29)) return false;
        }
    }
    void emitLeaf();
    void compileSibling(TokBuf &tb, std::string &buf, TokenRec &rec);
};

void Compiler::emitLeaf() {
    std::memcpy(ef_ + 40, &fs0_, 4);
    uint32_t h = 0x00489422, tl = 0xFFFFFFFF;
    std::memcpy(ef_ + 44, &h, 4);
    std::memcpy(ef_ + 48, &tl, 4);
    TokBuf tb = hb_ ? TokBuf{ef_, 0, 60, 56} : TokBuf{ef_, 0, 40, -1};
    tb.written = efWritten_;
    TokenRec rec;
    std::string buf = m_.nextToken(tb, &rec.start);
    rec.end = m_.pos;
    rec.text = buf;
    rec.w0 = size_t(m_.count);
    ++G.beh; ++G.ctb; ++G.cop; ++G.sip; ++G.dlr;
    auto first20 = [&]() { return buf.substr(0, 20); }; // local_38[20] = 0 before the strcat
    if (isIdentifier(buf)) {
        rec.kind = TokenRec::Identifier;
        int32_t off = lookupSymbol(buf);
        uint32_t v = m_.r32(off + 0x24);
        if (v == 0xFFFFFFFF) throw Thrown{std::string(kMsgBadId) + first20()};
        if (m_.mem[size_t(off) + 3] == 'P' && m_.mem[size_t(off) + 4] == 'R' && m_.mem[size_t(off) + 5] == 'N')
            v |= 0xFEDC0000;
        if (G.dlr == 1) {
            uint32_t tmp;
            bool undef = false;
            if (!msvcParsePlanNumber(buf, tmp, &undef)) throw Thrown{kMsgLocalsEnum};
        }
        emitWord(v);
        rec.special = off < kSymbols;
        if (!rec.special) {
            size_t k = size_t((off - kSymbols) / kEntry);
            if (k < syms_.symbols().size()) rec.group = syms_.symbols()[k].group;
        }
    } else if (isNumber(buf)) {
        rec.kind = TokenRec::Number;
        int32_t idx = m_.count++;
        uint32_t v = 0;
        bool undef = false;
        if (!msvcParsePlanNumber(buf, v, &undef)) throw Thrown{std::string(kMsgBadNumber) + first20()};
        if (undef && hb_)
            throw Hazard{"undefined", "'" + buf + "': sscanf(\"%x\") reaches the end of the token and the "
                                      "original emits uninitialised stack bytes"};
        putWord(idx, undef ? 0 : v);
    }
    if (hb_ && buf.size() >= 40 && buf.size() < 44 && tb.get(0) != '"')
        hbWarning("adlib1996-long-token", "token of " + std::to_string(buf.size()) + " bytes overwrites the "
                  "original's exception chain: an error later in this file would crash the game", rec.start);
    if (tb.get(0) == '"') {
        rec.kind = TokenRec::String;
        // FindLocalReal 0x489c20 copies the token into a 40-byte stack buffer.
        if (hb_ && buf.size() >= 40)
            throw Hazard{"crash", "string token of " + std::to_string(buf.size()) +
                                  " bytes (40 or more, quotes included) overflows a 40-byte buffer; "
                                  "the original compiler would crash"};
        std::string s = buf.size() >= 2 ? buf.substr(1, buf.size() - 2) : std::string();
        int found = tables_.findLocal(s);
        if (found >= 0) {
            emitWord(0xEDCB0000u | uint32_t(found & 0xFFFF));
            rec.str = TokenRec::LocalReal;
        } else if ((found = tables_.findInt(s)) >= 0) {
            emitWord(0xDCBA0000u | uint32_t(found & 0xFFFF));
            rec.str = TokenRec::InteractorName;
        } else if (G.beh == 3) {
            if (!emitString(tb, buf, rec)) throw Thrown{std::string(kMsgBadString) + buf};
            rec.str = TokenRec::BehName;
        } else if (G.cop == 2) {
            if (!emitString(tb, buf, rec)) throw Thrown{std::string(kMsgBadString) + buf};
            planName_ = stripInPlace(tb, buf);
            rec.str = TokenRec::PlanName;
        } else if (G.cop == 3) {
            compileSibling(tb, buf, rec);
            rec.str = TokenRec::PlanBehRef;
        } else if (G.ctb == 2) {
            std::string name = stripInPlace(tb, buf);
            emitWord(uint32_t(int32_t(tables_.findBehavior(name))));
            rec.str = TokenRec::BehRef;
        } else if (G.sip == 3) {
            std::string name = stripInPlace(tb, buf); // LookupInteractorName 0x48a160: names known
            int k = tables_.findInt(name);            // to FindInteractorName never get here
            if (k < 0) throw Thrown{std::string(kMsgNoInteractor) + name};
            emitWord(uint32_t(k));
            rec.str = TokenRec::InteractorRef;
        } else {
            if (!emitString(tb, buf, rec)) throw Thrown{std::string(kMsgBadString) + buf};
            rec.str = TokenRec::Plain;
        }
        if (rec.residue)
            hbWarning("adlib1996-stale-string", "string " + rec.text + " has no closing quote; the original finished it with "
                      "stale bytes of an earlier token", rec.start);
        if (rec.unknownRead)
            hbWarning("adlib1996-stale-unknown", "string " + rec.text + " has no closing quote; the original's search ran into "
                      "stack bytes left by code that ran before this compile (modelled as zero, as in a fresh "
                      "process): a quote byte among them would change the result", rec.start);
    } else {
        // Every non-string token is looked up as a keyword; the entry (or the "" terminator) is
        // copied over acStack_88, which is also where CHANGE_OF_PLAN keeps its plan name.
        int32_t off = findKeyword(buf);
        std::string name = m_.cstrAt(off);
        uint32_t tag = m_.r32(off + 0x20), id = m_.r32(off + 0x24);
        planName_ = name;
        if (id < 0x160001) {
            if (id == 0x160000) G.cop = 0;
            else if (id == 0x70000) G.beh = 0;
            else if (id == 0x70002) G.ctb = 0;
        } else if (id == 0x160001) {
            G.dlr = 0;
        } else if (id == 0x180001) {
            G.sip = 0;
        }
        if (!name.empty()) {
            emitWord(tag);
            emitWord(id);
            rec.kind = TokenRec::Keyword;
            rec.kwId = id;
        } else if (rec.kind != TokenRec::Identifier && rec.kind != TokenRec::Number) {
            rec.kind = buf.empty() ? TokenRec::Empty : (buf == "}" || buf == "{") ? TokenRec::Brace : TokenRec::Dropped;
        }
    }
    fs0_ = efR32(40);
    rec.w1 = size_t(m_.count);
    recs_.push_back(std::move(rec));
    if (tb.retClobbered)
        throw Hazard{"crash", "a token of 56 or more bytes overwrites EmitLeaf's return address; the "
                              "original compiler would crash"};
}

// cop == 3: a new CPlanCompiler (ctor, LoadSymbols, OpenPlanSource, PrescanDecls) finds the
// behaviour in Data\Plans\<plan>.txt.
void Compiler::compileSibling(TokBuf &tb, std::string &buf, TokenRec &rec) {
    std::string plan = planName_;
    std::optional<std::string> text;
    std::string key = lowerAscii(plan);
    auto it = planCache_.find(key);
    if (it != planCache_.end()) {
        text = it->second;
    } else {
        if (opt_.planSource) text = opt_.planSource(plan);
        planCache_[key] = text;
    }
    if (!text) throw Thrown{kMsgCannotOpen};
    Machine sub = syms_.image().d.m;
    sub.hb = hb_;
    Tables st{sub, hb_, {}, {}, {}};
    sub.readSource(*text);
    Prescan p{sub, st, nullptr};
    try {
        p.run();
    } catch (Hazard &h) {
        h.message = "in plan " + plan + ": " + h.message;
        throw;
    }
    std::string name = stripInPlace(tb, buf);
    emitWord(uint32_t(int32_t(st.findBehavior(name))));
    rec.group = plan;
}

CompileResult Compiler::run() {
    CompileResult res;
    auto lineOf = [&](int32_t off) {
        size_t lim = std::min<size_t>(size_t(std::max(off, 0)), source_.size());
        return int32_t(std::count(source_.begin(), source_.begin() + long(lim), '\n') + 1);
    };
    std::string stage = "read";
    auto hbNotes = [&]() { // always on in adlib1996 mode, also when an error follows
        if (!hb_) return;
        for (const auto &f : facts_.facts)
            if (f.code == "name-overflow" || f.code == "table-overflow") hbWarning("adlib1996-" + f.code, f.message, f.offset);
        if (m_.ranPastEnd)
            hbWarning("adlib1996-read-past-end", "the last line has no line end (a comment or bare CR on it): the original "
                      "reads past the end of the source into its buffer's stale contents (output unaffected; "
                      "error positions near the end come from those bytes)", int32_t(source_.size()));
        res.diagnostics.insert(res.diagnostics.end(), hbWarn_.begin(), hbWarn_.end());
    };
    auto fail = [&](const std::string &kind, const std::string &msg) {
        hbNotes();
        Diagnostic d;
        d.stage = stage;
        d.kind = kind;
        d.message = msg;
        d.offset = m_.pos;
        d.line = lineOf(m_.pos);
        res.diagnostics.push_back(d);
        res.ok = false;
        return res;
    };
    m_.pos = syms_.image().d.loadPos;
    try {
        m_.readSource(source_);
        stage = "compile";
        Prescan p{m_, tables_, &facts_};
        p.run();
        buildIndex(); // after the prescan, which may have overwritten tables
        std::memcpy(ef_, p.pf, 40); // prescan's frame is what EmitLeaf's token buffer starts with
        std::memcpy(efWritten_, p.pfWritten, 40);
        for (int i = 40; i < 60; ++i) efWritten_[i] = 1;
        m_.pos = 0;
        while (!(m_.size <= m_.pos) && m_.cur() != 0) emitLeaf();
    } catch (const Thrown &t) {
        return fail("throw", t.message);
    } catch (const Hazard &h) {
        std::string msg = h.message;
        if (msg.find("original") == std::string::npos) msg += " (the original compiler would crash)";
        return fail(h.kind, msg);
    }
    res.words.assign(m_.out.begin(), m_.out.begin() + m_.count);
    auto endIt = std::find(res.words.begin(), res.words.end(), 0xFFFF0000u);
    if (hb_ && endIt != res.words.end() && endIt + 1 != res.words.end())
        hbWarning("adlib1996-after-end", std::to_string(res.words.end() - endIt - 1) +
                  " dwords after END_PLAN are counted in the PBN header but not written: the game's "
                  "loader would read past the end of the file", -1);
    hbNotes(); // always on in adlib1996 mode: accepted by the original, but not what the source says
    if (opt_.strict) strictAnalysis(source_, syms_, recs_, res.words, facts_, hb_, res.diagnostics);
    if (endIt == res.words.end()) {
        Diagnostic d;
        d.stage = "write";
        d.kind = "no-end";
        d.message = kMsgNoEnd;
        res.diagnostics.insert(res.diagnostics.begin(), d);
        res.ok = false;
        return res;
    }
    uint32_t n = uint32_t(res.words.size());
    auto put = [&](uint32_t v) { for (int i = 0; i < 4; ++i) res.pbn.push_back(uint8_t(v >> (8 * i))); };
    put(n);
    for (auto w = res.words.begin(); w != endIt + 1; ++w) put(*w);
    res.ok = true;
    if (opt_.werror)
        for (const Diagnostic &d : res.diagnostics)
            if (d.severity == Diagnostic::Severity::Warning) { res.ok = false; break; }
    return res;
}

} // namespace

CompileResult compilePlan(std::string_view source, const CompilerSymbols &symbols, const CompileOptions &opt) {
    Compiler c(symbols, opt, source);
    return c.run();
}

} // namespace adlib
