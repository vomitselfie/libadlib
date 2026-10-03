// Internal: an emulation of one CPlanCompiler object (HYPERX.EXE 1996) at byte level.
//
// The original keeps its name tables, keyword table, special-constant table and symbol table in
// one 0x2289C-byte object, copies strings with unbounded strcpy, and tokenizes into fixed stack
// buffers. Malformed input therefore interacts with neighbouring memory in ways that change the
// output; this model keeps the same layout so those interactions are reproduced. Offsets below
// are the exe's (docs/research/binary-evidence.md §2).
#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "adlib/compiler.h"

namespace adlib::cc {

constexpr int32_t kObjSize = 0x2289C;
constexpr int32_t kBehNames = 0x18;     // char[60][32]
constexpr int32_t kLocalNames = 0x798;  // char[60][32]
constexpr int32_t kIntNames = 0xF18;    // char[150][32]
constexpr int32_t kKeywords = 0x21DC;   // 28 x {char name[32]; u32 tag; u32 id}
constexpr int32_t kSpecials = 0x3DFC;   // 58 x {char name[36]; u32 value}
constexpr int32_t kSymbols = 0x4A7C;    // n x {char name[36]; i32 value}
constexpr int32_t kMsgTable = 0x1929C;  // char[61][20][32] (dev-dialog list box only)
constexpr int32_t kEntry = 0x28;
constexpr int32_t kSrcCap = 80000;      // new(80000)
constexpr int32_t kOutCap = 30000;      // new(120000) dwords
constexpr int32_t kMaxSource = 79999;   // ReadSource throws when this many bytes were read

// Messages, verbatim (.data/.rdata strings, trailing newlines included).
inline constexpr const char *kMsgTooLarge =
    "This plan file is too large.  Why not break it up into separate plans?\n";
inline constexpr const char *kMsgCannotOpen = "Plan file could not be opened.\n";
inline constexpr const char *kMsgArgEnum = "Expected an argument enumerator.\n";
inline constexpr const char *kMsgArgEnumLocals =
    "Expected an argument enumerator.  First two args are id and string name.\n";
inline constexpr const char *kMsgBehType = "Expected an id_BEH_ type id.\n";
inline constexpr const char *kMsgBehName = "Expected a string name in Behavior Declaration.\n";
inline constexpr const char *kMsgMsgType = "Expected an id_MSG_ type id.\n";
inline constexpr const char *kMsgLocalName = "Expected a string name in LocalVar Declaration.\n";
inline constexpr const char *kMsgLocalsEnum = "expected arg enumeration of localvars.";
inline constexpr const char *kMsgBadId = "Bad plan id value: ";
inline constexpr const char *kMsgBadNumber = "Bad number in plan: ";
inline constexpr const char *kMsgBadString = "Inability to read string: ";
inline constexpr const char *kMsgNoBehavior = "Could not find behavior named: ";
inline constexpr const char *kMsgNoInteractor = "Could not find interactor named: ";
inline constexpr const char *kMsgNoEnd =
    "no END_PLAN (0xFFFF0000) in output; the original WritePbn would overrun its buffer";

// The original's `throw "message"`.
struct Thrown {
    std::string message;
};
// Input the original cannot survive (crash, memory corruption, undefined value). kind is
// "crash", "corrupt" or "undefined".
struct Hazard {
    std::string kind;
    std::string message;
};

// MSVC 4.x CRT emulation (numbers.cpp).
bool msvcParsePlanNumber(const std::string &tok, uint32_t &out, bool *undefined = nullptr);
int32_t msvcAtol(const char *s);
int msvcStricmp(const char *a, const char *b);

// A token buffer living in an emulated stack frame: `frame[base + i]` for i < cap, and the
// index of the first byte that would land on a saved return address (`retIndex`, or -1).
struct TokBuf {
    uint8_t *frame;
    int32_t base;
    int32_t cap;      // bytes available in the frame from base
    int32_t retIndex; // token index that overwrites the return address (crash on return)
    int32_t maxWritten = -1;
    bool retClobbered = false;
    uint8_t *written = nullptr; // optional: marks the bytes this compile has written (index base+i)
    void put(int32_t i, uint8_t c) {
        if (i < 0) { retClobbered = true; return; } // short index wrapped: writes below the frame
        if (i > maxWritten) maxWritten = i;
        if (retIndex >= 0 && i >= retIndex) retClobbered = true;
        if (i < cap) {
            frame[base + i] = c;
            if (written) written[base + i] = 1;
        }
    }
    uint8_t get(int32_t i) const { return i < cap ? frame[base + i] : 0; }
    const char *cstr() const { return reinterpret_cast<const char *>(frame + base); }
};

struct Machine {
    std::vector<uint8_t> mem; // object memory
    std::vector<uint8_t> src; // source buffer (kSrcCap), residue of earlier reads kept
    int32_t size = 0, pos = 0, count = 0;
    std::vector<uint32_t> out;
    int32_t objEnd = kObjSize;
    int32_t msgBase = kMsgTable;
    int32_t symEnd = kSymbols; // one past the terminator entry
    bool hb = true;            // 1996 machine limits (Compat::Adlib1996; false: Compat::Adlib, limits lifted)
    bool ranPastEnd = false;   // SkipLine went past the end of the source (reads stale buffer)

    // Bytes after the 80000-byte source buffer: in the oracle's heap the 120000-byte output
    // buffer follows directly. Reads past that are not modelled.
    uint8_t byteAt(int64_t p) const {
        if (p < 0) return 0;
        if (!hb) return p < int64_t(src.size()) && p <= size ? src[size_t(p)] : 0;
        if (p < kSrcCap) return src[size_t(p)];
        int64_t q = p - kSrcCap;
        if (q < int64_t(kOutCap) * 4) {
            size_t w = size_t(q / 4);
            uint32_t v = w < out.size() ? out[w] : 0;
            return uint8_t(v >> (8 * (q % 4)));
        }
        throw Hazard{"hang", "the original compiler would scan past the end of its source buffer "
                             "looking for a line end (a comment or bare CR at the end of the file)"};
    }
    uint8_t cur() const { return byteAt(pos); }

    // ---- tokenizer (exact) ----
    bool isSep() const { uint8_t c = cur(); return c == ' ' || c == '\t' || c == ',' || c == '&'; }
    bool isComment() const {
        uint8_t c = cur();
        if (c == '/') { uint8_t d = byteAt(int64_t(pos) + 1); if (d == '/' || d == '*') return true; }
        return c == '(' || c == '#';
    }
    void skipLine() { // FUN_00489ac0: no end-of-buffer check
        if (!hb) { // Compat::Adlib: the end of the source ends the line
            while (pos < size && cur() != '\r' && cur() != '\n') ++pos;
            if (pos < size && cur() == '\r') ++pos;
            while (pos < size && cur() != '\r' && cur() != '\n') ++pos;
            if (pos < size && cur() == '\n') ++pos;
            return;
        }
        while (cur() != '\r' && cur() != '\n') ++pos;
        if (cur() == '\r') ++pos;
        while (cur() != '\r' && cur() != '\n') ++pos;
        if (cur() == '\n') ++pos;
        if (pos > size) ranPastEnd = true;
    }
    void skipSeps() { while (isSep()) ++pos; }
    // CPlanCompiler_NextToken 0x489940. Returns the token (bytes up to the NUL it wrote).
    // `start` receives the offset of its first byte.
    std::string nextToken(TokBuf &tb, int32_t *start = nullptr) {
        int16_t n = 0;
        bool comment = false, nl = false, brace = false;
        std::string tok;
        skipSeps();
        if (start) *start = pos;
        for (;;) {
            if (n != 0) { tb.put(n, 0); return tok; }
            bool sep = false;
            for (;;) {
                if (isSep()) { sep = true; break; }
                comment = isComment();
                if (comment) break;
                uint8_t c = cur();
                nl = (c == '\r' || c == '\n');
                if (nl) break;
                brace = c == '}';
                if (brace || size < pos || c == 0) break;
                if (n == 0 && start) *start = pos;
                tb.put(n, c);
                tok.push_back(char(c));
                ++n;
                ++pos;
            }
            if (sep) skipSeps();
            if (comment || nl) { skipLine(); nl = false; comment = false; }
            if (brace && n == 0) {
                if (start) *start = pos;
                tb.put(0, cur());
                ++pos;
                tb.put(1, 0);
                return "}";
            }
            if (size < pos || cur() == 0) { tb.put(n, 0); return tok; }
        }
    }

    // ---- object memory helpers ----
    void need(int64_t off, int64_t len, const char *what) const {
        if (off < 0 || off + len > objEnd)
            throw Hazard{"corrupt", std::string("the original compiler would write past the end of "
                                                "its object (heap corruption): ") + what};
    }
    void strcpyTo(int32_t off, const char *s, const char *what) {
        size_t n = std::strlen(s) + 1;
        need(off, int64_t(n), what);
        std::memcpy(&mem[size_t(off)], s, n);
    }
    void w32(int32_t off, uint32_t v) { std::memcpy(&mem[size_t(off)], &v, 4); }
    uint32_t r32(int32_t off) const { uint32_t v; std::memcpy(&v, &mem[size_t(off)], 4); return v; }
    // NUL-terminated string at off (bounded by the modelled object).
    std::string cstrAt(int32_t off) const {
        std::string s;
        for (int32_t i = off; i < int32_t(mem.size()) && mem[size_t(i)]; ++i) s.push_back(char(mem[size_t(i)]));
        return s;
    }
    void clearNameTables() { // ReadSource: first byte of every slot
        for (int i = 0; i < 60; ++i) {
            mem[size_t(kBehNames + i * 32)] = 0;
            mem[size_t(kLocalNames + i * 32)] = 0;
            for (int j = 0; j < 20; ++j) mem[size_t(msgBase + (j + i * 20) * 32)] = 0;
        }
        for (int i = 0; i < 150; ++i) mem[size_t(kIntNames + i * 32)] = 0;
    }
    // CPlanCompiler_ReadSource 0x488490 (throws Thrown on >= 79999 bytes).
    void readSource(std::string_view data) {
        size = 0;
        ranPastEnd = false;
        size_t n = data.size();
        if (!hb) { // Compat::Adlib: no 79999-byte limit
            if (src.size() < n + 2) src.resize(n + 2, 0);
            std::memcpy(src.data(), data.data(), n);
            size = int32_t(n);
            src[n] = 0;
            pos = 0;
            count = 0;
            clearNameTables();
            return;
        }
        size_t take = n < size_t(kMaxSource) ? n : size_t(kMaxSource);
        std::memcpy(src.data(), data.data(), take);
        size = int32_t(take);
        if (n >= size_t(kMaxSource)) throw Thrown{kMsgTooLarge};
        src[size_t(size)] = 0;
        pos = 0;
        count = 0;
        clearNameTables();
    }
};

// The prebuilt machine state after CPlanCompiler_ctor + LoadSymbols.
struct ImageData {
    Machine m;
    int32_t loadPos = 0; // cursor left by LoadSymbols (reported by a "too large" error)
    bool memoryNames = true; // lookups see the names as stored in the 36-byte fields
};

} // namespace adlib::cc

struct adlib::CompilerSymbols::Image {
    adlib::cc::ImageData d;
};
