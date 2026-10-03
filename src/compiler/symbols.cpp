// CPlanCompiler_ctor (0x487790) tables and CPlanCompiler_LoadSymbols (0x48a240), emulated.
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "adlib/vocabulary.h"
#include "internal.h"
#include "machine.h"

namespace adlib {

using namespace cc;

namespace {
const CompilerKeyword kKeywordTable[] = {
    {"SET_COLLISION_INTERACTOR", 0x30000}, {"SET_MESSAGE_INTERACTOR", 0x50000},
    {"SET_LOC_COLLISION_INTERACTOR", 0x30001}, {"SET_NET_COLLISION_INTERACTOR", 0x50002},
    {"SET_LOC_MESSAGE_INTERACTOR", 0x50001}, {"SET_NET_MESSAGE_INTERACTOR", 0x50002},
    {"ADD_AGENDA_ITEM", 0x1a0001}, {"REMOVE_AGENDA_ITEM", 0x1a0003}, {"SET_TIMEOUTMSG", 0x1a0002},
    {"GLOBAL_INTERACTORS", 0x1b0000}, {"CHANGE_OF_PLAN", 0x160000}, {"CHANGE_TO_BEHAVIOR", 0x70002},
    {"DECLARE_BEHAVIOR", 0x70000}, {"DECLARE_LOCAL_REALS", 0x160001}, {"SET_BEHAVIOR_PARAM", 0x70003},
    {"SET_INTERACTOR_PARAM", 0x180001}, {"DO_ACTION", 0x180000}, {"DO_LOCAL_ACTION", 0x180002},
    {"DO_NET_ACTION", 0x180003}, {"DECIDE_BY_AMONG", 0x170000}, {"DECIDE_BY_WITH_AMONG", 0x170001},
    {"SET_DEBUG", 0x60001}, {"SET_TRACE", 0x60002}, {"Enable", 0x80000000u}, {"Block", 0x40000000},
    {"DataBlock", 0x20000000}, {"END_PLAN", 0xffff0000u},
};

bool readFileCI(const std::string &dir, const std::string &name, std::string &out) {
    // Case-insensitive lookup of `name` (may contain one '/' level) under dir.
    std::string d = dir, n = name;
    size_t slash = n.find('/');
    if (slash != std::string::npos) {
        std::string sub;
        DIR *h = opendir(d.c_str());
        if (!h) return false;
        while (dirent *e = readdir(h))
            if (msvcStricmp(e->d_name, n.substr(0, slash).c_str()) == 0) { sub = e->d_name; break; }
        closedir(h);
        if (sub.empty()) return false;
        return readFileCI(d + "/" + sub, n.substr(slash + 1), out);
    }
    DIR *h = opendir(d.c_str());
    if (!h) return false;
    std::string found;
    while (dirent *e = readdir(h))
        if (msvcStricmp(e->d_name, n.c_str()) == 0) { found = e->d_name; break; }
    closedir(h);
    if (found.empty()) return false;
    std::ifstream f(d + "/" + found, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// The ctor: keyword and special tables, empty buffers.
Machine freshMachine() {
    Machine m;
    m.mem.assign(size_t(kObjSize), 0);
    m.src.assign(size_t(kSrcCap), 0);
    int32_t off = kKeywords;
    for (const CompilerKeyword &k : kKeywordTable) {
        std::memcpy(&m.mem[size_t(off)], k.name, std::strlen(k.name) + 1);
        m.w32(off + 0x20, 0xABCDEFFF);
        m.w32(off + 0x24, k.id);
        off += kEntry;
    }
    m.w32(off + 0x20, 0xFFFFFFFF); // terminator: name "" (0x50CA90)
    m.w32(off + 0x24, 0xFFFFFFFF);
    off = kSpecials;
    for (const auto &s : compilerSpecials()) {
        std::memcpy(&m.mem[size_t(off)], s.first.c_str(), s.first.size() + 1);
        m.w32(off + 0x24, s.second);
        off += kEntry;
    }
    m.w32(off + 0x24, 0xFFFFFFFF);
    return m;
}

std::string enumNameBefore(const std::string &text, size_t brace) {
    size_t e = brace;
    while (e > 0 && std::isspace(uint8_t(text[e - 1]))) --e;
    size_t b = e;
    while (b > 0 && (std::isalnum(uint8_t(text[b - 1])) || text[b - 1] == '_')) --b;
    return text.substr(b, e - b);
}
} // namespace

const std::vector<CompilerKeyword> &compilerKeywords() {
    static const std::vector<CompilerKeyword> v(std::begin(kKeywordTable), std::end(kKeywordTable));
    return v;
}

const std::vector<std::pair<std::string, uint32_t>> &compilerSpecials() {
    static const std::vector<std::pair<std::string, uint32_t>> v = [] {
        std::vector<std::pair<std::string, uint32_t>> r = {
            {"_BROADCAST_", 0xffff}, {"_STRAIGHTAHEAD_", 0xabcdabcd}, {"_TEMPORARILY_", 0xabcdabcc},
            {"_MESSAGE_DATA_", 0xabcdabce}, {"_NO_BEH_CHANGE_", 0xabcdabcb}, {"_PREVIOUS_BEH_", 0xabcdabca}};
        for (int i = 0; i <= 50; ++i) r.push_back({"_" + std::to_string(i) + "_", uint32_t(i)});
        return r;
    }();
    return v;
}

// ---- CompilerSymbols ---------------------------------------------------------------------------

CompilerSymbols::CompilerSymbols() : img_(new Image) {
    img_->d.m = freshMachine();
    for (int i = 0; i < int(Role::Count); ++i) roleGroups_.push_back(defaultEnumForRole(Role(i)));
}

const std::string &CompilerSymbols::roleGroup(int role) const {
    static const std::string none;
    return role >= 0 && size_t(role) < roleGroups_.size() ? roleGroups_[size_t(role)] : none;
}
CompilerSymbols::~CompilerSymbols() { delete img_; }
CompilerSymbols::CompilerSymbols(const CompilerSymbols &o)
    : syms_(o.syms_), img_(new Image(*o.img_)), roleGroups_(o.roleGroups_), warnings_(o.warnings_) {}
CompilerSymbols &CompilerSymbols::operator=(const CompilerSymbols &o) {
    if (this != &o) { syms_ = o.syms_; *img_ = *o.img_; roleGroups_ = o.roleGroups_; warnings_ = o.warnings_; }
    return *this;
}
CompilerSymbols::CompilerSymbols(CompilerSymbols &&o) noexcept
    : syms_(std::move(o.syms_)), img_(o.img_), roleGroups_(std::move(o.roleGroups_)),
      warnings_(std::move(o.warnings_)) {
    o.img_ = nullptr;
}
CompilerSymbols &CompilerSymbols::operator=(CompilerSymbols &&o) noexcept {
    std::swap(syms_, o.syms_);
    std::swap(img_, o.img_);
    std::swap(roleGroups_, o.roleGroups_);
    std::swap(warnings_, o.warnings_);
    return *this;
}

const CompilerSymbol *CompilerSymbols::find(std::string_view name) const {
    std::string n(name);
    for (const CompilerSymbol &s : syms_)
        if (msvcStricmp(s.name.c_str(), n.c_str()) == 0) return &s;
    return nullptr;
}

// Builder that appends entries exactly like LoadSymbols: strcpy(name) at entry k, value at +0x24.
struct SymbolsBuilder {
    Machine &m;
    std::vector<CompilerSymbol> &syms;
    int32_t k = 0;
    void add(const std::string &name, int32_t value, const std::string &group) {
        int32_t off = kSymbols + k * kEntry;
        size_t need = size_t(off) + name.size() + 1 + kEntry;
        if (m.mem.size() < need) m.mem.resize(need + 0x10000, 0);
        std::memcpy(&m.mem[size_t(off)], name.c_str(), name.size() + 1);
        m.w32(off + 0x24, uint32_t(value));
        syms.push_back({name, value, group});
        ++k;
    }
    void finish(bool memoryNames, std::vector<std::string> *warnings) {
        int32_t off = kSymbols + k * kEntry;
        if (m.mem.size() < size_t(off + kEntry)) m.mem.resize(size_t(off + kEntry), 0);
        m.mem[size_t(off)] = 0;
        m.w32(off + 0x24, 0xFFFFFFFF);
        m.symEnd = off + kEntry;
        // The original's capacity is (0x1929C - 0x4A7C) / 0x28 = 2100 symbols; beyond that its
        // table would run into the dev-dialog interactor table. Larger (new-game) vocabularies get
        // the interactor table moved behind the symbols instead.
        m.msgBase = m.symEnd <= kMsgTable ? kMsgTable : ((m.symEnd + 31) & ~31);
        m.objEnd = m.msgBase + 61 * 20 * 32;
        if (m.mem.size() < size_t(m.objEnd)) m.mem.resize(size_t(m.objEnd), 0);
        m.mem.resize(size_t(m.objEnd));
        // In the original a name of 36+ characters runs into its value field (and the next entry),
        // so lookups see a different string. Original limits: record what lookups see. New games:
        // keep the name as written and say so.
        for (int32_t i = 0; i < k; ++i) {
            if (syms[size_t(i)].name.size() < 36) continue;
            if (memoryNames) syms[size_t(i)].name = m.cstrAt(kSymbols + i * kEntry);
            else if (warnings)
                warnings->push_back("symbol " + syms[size_t(i)].name + " is longer than 35 characters: the "
                                    "original compiler could not have looked it up");
        }
    }
};

CompilerSymbols CompilerSymbols::fromFiles(const SymbolFiles &files, bool originalLimits) {
    CompilerSymbols r;
    r.img_->d.memoryNames = originalLimits;
    Machine &m = r.img_->d.m;
    m.hb = originalLimits; // new games: no 79999-byte limit, no scanning past a file's end
    SymbolsBuilder b{m, r.syms_};
    uint8_t frame[64] = {};
    auto tok = [&]() {
        TokBuf tb{frame, 0, 64, -1};
        std::string t = m.nextToken(tb);
        if (t.size() >= 40) {
            std::string msg = "symbol file token '" + t + "' is 40 bytes or longer: the original "
                              "compiler's LoadSymbols would overwrite its counters";
            if (originalLimits) throw std::runtime_error(msg);
            r.warnings_.push_back(msg);
        }
        return t;
    };
    auto read = [&](const std::optional<std::string> &f) {
        m.size = 0;
        try {
            m.readSource(*f);
        } catch (const Thrown &) {
            throw std::runtime_error("symbol file of 79999 bytes or more: the original throws \"This plan "
                                     "file is too large\" while loading symbols");
        }
    };
    int16_t idx = 0;
    // A missing file is skipped. (The original would re-scan the previous file's buffer; with the
    // cursor already at its end that adds nothing, except that a missing enumIDs.h makes it scan
    // uninitialised memory for a '{'.)
    // 1. enumIDs.h
    if (files.enumIds) {
    read(files.enumIds);
    std::string text = *files.enumIds;
    m.pos = 0;
    while (m.cur() != '{') {
        ++m.pos;
        if (m.pos > m.size) {
            if (originalLimits) throw std::runtime_error("enumIDs.h has no '{': the original LoadSymbols would scan past its buffer");
            r.warnings_.push_back("enumIDs.h has no '{' (no enum): the original LoadSymbols would scan past its buffer");
            break;
        }
    }
    std::string group = m.pos < int32_t(text.size()) ? enumNameBefore(text, size_t(m.pos)) : "";
    ++m.pos;
    if (m.pos <= m.size) {
        do {
            if (m.cur() == 0) break;
            std::string t = tok();
            if (frame[0] == '}') {
                idx = 0;
                if (m.cur() != '{') {
                    for (;;) {
                        int32_t p = m.pos;
                        if (m.size < p || m.byteAt(p) == 0) break;
                        m.pos = p + 1;
                        if (m.byteAt(p + 1) == '{') break;
                    }
                }
                if (m.pos < int32_t(text.size()) && text[size_t(m.pos)] == '{')
                    group = enumNameBefore(text, size_t(m.pos));
                ++m.pos;
            } else {
                b.add(t, idx++, group);
            }
        } while (m.pos <= m.size);
    }
    }
    // 2. Animassm.txt
    if (files.animAssm) {
    read(files.animAssm);
    idx = 0;
    m.skipLine();
    if (m.pos <= m.size) {
        do {
            if (m.cur() == 0) break;
            std::string t = tok();
            if (t.size() >= 3 && t[0] == 'A' && t[1] == 'A' && t[2] == '_') b.add(t, idx++, "Animassm");
            else m.skipLine();
        } while (m.pos <= m.size);
    }
    }
    // 3. Sndfiles.lst
    if (files.sndFiles) {
    read(files.sndFiles);
    idx = 0;
    if (m.pos <= m.size) {
        do {
            if (m.cur() == 0) break;
            if (m.isComment()) m.skipLine();
            std::string t = tok();
            char c = t.empty() ? 0 : t[0];
            bool num = (c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-';
            if (!num) b.add("id_SND_" + t, idx++, "Sndfiles");
            else m.skipLine();
        } while (m.pos <= m.size);
    }
    }
    // 4. Infobtxt.lst
    if (files.infobTxt) {
    read(files.infobTxt);
    idx = 0;
    if (m.pos <= m.size) {
        do {
            if (m.cur() == 0) break;
            if (!m.isComment()) {
                std::string t = tok();
                b.add("id_TXT_" + t, idx++, "Infobtxt");
            }
            m.skipLine();
        } while (m.pos <= m.size);
    }
    }
    b.finish(originalLimits, &r.warnings_);
    r.img_->d.loadPos = m.pos;
    m.hb = true; // the compile sets its own mode
    return r;
}

CompilerSymbols CompilerSymbols::fromDataDir(const std::string &dir, std::string *err, bool originalLimits) {
    SymbolFiles f;
    std::string t;
    if (readFileCI(dir, "enumIDs.h", t)) f.enumIds = t;
    if (readFileCI(dir, "AnimData/Animassm.txt", t)) f.animAssm = t;
    if (readFileCI(dir, "Sndfiles.lst", t)) f.sndFiles = t;
    if (readFileCI(dir, "Infobtxt.lst", t)) f.infobTxt = t;
    if (!f.enumIds && err) *err = "no enumIDs.h in " + dir;
    return fromFiles(f, originalLimits);
}

CompilerSymbols CompilerSymbols::fromVocabulary(const Vocabulary &vocab) {
    CompilerSymbols r;
    Machine &m = r.img_->d.m;
    SymbolsBuilder b{m, r.syms_};
    r.img_->d.memoryNames = false;
    for (const std::string &nsName : vocab.namespaceNames()) {
        const Namespace *ns = vocab.find(nsName);
        const auto &names = ns->names();
        for (size_t i = 0; i < names.size(); ++i)
            if (!names[i].empty()) b.add(names[i], int32_t(i), nsName);
    }
    b.finish(false, &r.warnings_);
    for (int i = 0; i < int(Role::Count); ++i) r.roleGroups_[size_t(i)] = vocab.binding(Role(i));
    return r;
}

} // namespace adlib
