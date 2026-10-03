// Internal: what the compiler records for --strict analysis.
#pragma once
#include <string>
#include <vector>

#include "adlib/compiler.h"

namespace adlib::cc {

// One EmitLeaf call (one source token) as translated.
struct TokenRec {
    enum Kind : uint8_t { Empty, Keyword, Identifier, Number, String, Dropped, Brace };
    // How a string token was emitted.
    enum StrMode : uint8_t { NotString, Plain, LocalReal, InteractorName, BehName, PlanName,
                             PlanBehRef, BehRef, InteractorRef };
    Kind kind = Empty;
    StrMode str = NotString;
    std::string text;
    int32_t start = 0, end = 0; // source offsets
    uint32_t kwId = 0;          // Keyword
    size_t w0 = 0, w1 = 0;      // words emitted: [w0, w1)
    std::string group;          // Identifier: symbol group ("" for specials)
    bool special = false;       // Identifier from the special-constant table
    bool residue = false;       // String closed by a quote found past the token (stale buffer)
    bool unknownRead = false;   // the quote search read stack bytes this compile never wrote
};

// Things the prescan noticed (only the facts strict mode reports).
struct PrescanFacts {
    struct Fact { std::string code, message; int32_t offset; };
    std::vector<Fact> facts;
};


void strictAnalysis(std::string_view source, const CompilerSymbols &syms,
                    const std::vector<TokenRec> &recs, const std::vector<uint32_t> &words,
                    const PrescanFacts &pre, bool adlib1996, std::vector<Diagnostic> &out);

} // namespace adlib::cc
