// adlib-tools: inspection / authoring tools around libadlib (docs/tools.md).
//
//   SymbolTable  the compiler's view of a vocabulary (ordered, case-insensitive first match), so a
//                decompiler only prints a name when the compiler would turn it back into the
//                same number.
//   decompile    PBN -> ADLIB compiler source that recompiles byte-identically.
//   analyze      behaviour/transition model of a plan -> graph (DOT, Mermaid), inspect, lint.
//   compare      structural diff of two plans with tree paths.
//   Tracer       a structured trace sink for the runtime (Registry::traceEvent) with filters.
//
// Library: adlib_tools (links adlib). CLI: tools/adlib-cli.cpp (`adlib <command>`).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "adlib/pbn.h"
#include "adlib/runtime.h"
#include "adlib/vocabulary.h"

namespace adlib::tools {

// =============================================================================================
// Symbols
// =============================================================================================
class SymbolTable {
public:
    struct Entry { std::string name; int value; std::string ns; };

    SymbolTable();
    // Appends a symbol (compiler lookup order = order of add). Also adds it to vocab()'s `ns`.
    void add(const std::string &name, int value, const std::string &ns);
    // The compiler's lookup: case-insensitive, first match wins. nullptr when unknown.
    const Entry *lookup(std::string_view name) const;
    // Would the original compiler classify `name` as an identifier (prefix id / _ / A_ / AA_)?
    static bool isIdentifier(std::string_view name);
    // Does the compiler tag this symbol as a pronoun (0xFEDC) ("PRN" at [3..5])?
    static bool isPronounName(std::string_view name);
    // Name of value `v` in namespace `ns` that compiles back to `v` ("" when none).
    std::string nameIn(const std::string &ns, int v) const;
    std::string roleName(Role r, int v) const; // nameIn(binding(r))
    // Display name (no round-trip guarantee): short name or number.
    std::string displayName(Role r, int v) const;

    Vocabulary &vocab() { return *vocab_; }
    const Vocabulary &vocab() const { return *vocab_; }
    std::shared_ptr<Vocabulary> vocabPtr() const { return vocab_; }
    const std::vector<Entry> &entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }

    // Argument typing for DO_ACTION values: (action short name, child index) -> namespace.
    // Default: SendMessage #2 -> Message role. `addHint("Display:1=Texts")` adds one (a game with
    // its own message-sending or sound actions adds hints for them).
    std::map<std::pair<std::string, int>, std::string> argHints;
    bool addHint(const std::string &spec, std::string *err = nullptr);
    // Is argument `i` of action `acf` (short name) a message id (hint "@Message")?
    bool isMessageArg(const std::string &acf, int i) const;

private:
    std::shared_ptr<Vocabulary> vocab_;
    std::vector<Entry> entries_;
    std::map<std::string, size_t> first_; // lower-case name -> entry index
};

// The 1996 compiler's symbol load (CPlanCompiler_LoadSymbols 0x48a240) from a directory in its
// layout: enumIDs.h, then AnimData/Animassm.txt (AA_*), Sndfiles.lst (id_SND_<w>), Infobtxt.lst
// (id_TXT_<w>), using the original tokenizer. Namespaces: enum names, "AnimAssm", "Sounds",
// "InfoText".
bool loadCompilerSymbolsDir(SymbolTable &t, const std::string &dataDir, std::string *err);
// The same from individual files (adlibc's --enumids/--anims/--sounds/--texts); "" = skip.
bool loadCompilerSymbolFiles(SymbolTable &t, const std::string &enumIds, const std::string &animAssm,
                             const std::string &sndFiles, const std::string &infobTxt, std::string *err);
// An enum header (Vocabulary::loadEnumHeader rules).
bool loadEnumFile(SymbolTable &t, const std::string &path, std::string *err);
// One symbol per line (first token; '#' comments) into namespace `ns`.
bool loadListFile(SymbolTable &t, const std::string &ns, const std::string &path, std::string *err);
// A directory: the 1996 layout (enumIDs.h + AnimData/) -> loadCompilerSymbolsDir; otherwise every *.H
// (enum headers, sorted) then every *.LST (lists; namespace = upper-case file stem; the stem
// "VENDTEXT" style is kept as is).
bool loadSymbolsDir(SymbolTable &t, const std::string &dir, std::string *err);

// =============================================================================================
// Decompiler
// =============================================================================================
struct DecompileOptions {
    std::string indent = "    ";
    bool annotate = false;   // node info comments (behaviour indices, decision branch values, notes)
    bool annotateOffsets = false; // also the dword offset of every statement ("@N", header = 0)
    bool header = true;      // leading comment block
    bool crlf = false;
    std::string planName;    // for the header
    // Behaviour names of other plans (upper-case plan name -> names), so CHANGE_OF_PLAN targets
    // are written by name. Requires the sibling source at compile time (grammar.md §5.3).
    const std::map<std::string, std::vector<std::string>> *planBehaviours = nullptr;
};
// Behaviour names in declaration order as the compiler's prescan sees them (spaces -> '_').
std::vector<std::string> behaviourNames(const std::vector<uint32_t> &words);
// The original compiler reads at most 79999 source bytes ("This plan file is too large").
constexpr size_t kMaxCompilerSource = 79998;
// Words (no u32 header) -> source text. False + err when the stream cannot be expressed
// (e.g. a string containing '_'). On success `err` (if given) receives a warning when the text is
// larger than kMaxCompilerSource, and is cleared otherwise.
bool decompile(const std::vector<uint32_t> &words, const SymbolTable &syms, const DecompileOptions &opt,
               std::string &out, std::string *err);
// Shortest decimal with '.' that round-trips to the float bits, else "0x%08x".
std::string formatFloatLiteral(uint32_t bits);

// =============================================================================================
// Analysis: behaviours and transitions
// =============================================================================================
struct Transition {
    enum class Kind { Behaviour, Previous, Plan };
    int from = -1;          // behaviour index, -1 = GLOBAL_INTERACTORS (any behaviour)
    Kind kind = Kind::Behaviour;
    int to = -1;            // behaviour index (Behaviour), behaviour index in `plan` (Plan)
    std::string plan;       // CHANGE_OF_PLAN target plan
    std::string toName;     // resolved behaviour name ("" unknown)
    std::string trigger;    // "MSG Coin", "COLL Body/Ball", "INIT" for setup
    std::string cond;       // decision path "EnoughCredit=1, InStock=0" ("" = unconditional)
    bool viaOption = false; // interactor's own CHANGE_TO_BEHAVIOR option (vs. inside Enable)
    std::string path;       // tree path of the CHANGE_* node
};
struct Timer { int beh; std::string trigger; int32_t delay; std::string msg; std::string recipient; };
struct BehaviourInfo {
    int index = 0;
    std::string name;
    int type = -1;
    std::string typeName;
    std::vector<std::string> triggers; // interactor triggers ("MSG x", "COLL a/b")
    int params = 0;
    bool fallthrough = false; // no interactors: the runtime falls through to the next one
};
struct PlanModel {
    std::string name;
    std::vector<BehaviourInfo> behaviours;
    std::vector<std::string> globalTriggers;
    std::vector<Transition> transitions;
    std::vector<Timer> timers;
};
// `tree` loaded with identity hooks (LoadHooks{}), so pronouns and plan names stay symbolic.
PlanModel analyze(const PlanTree &tree, const SymbolTable *syms,
                  const std::map<std::string, std::vector<std::string>> *planBehaviours = nullptr);

struct GraphOptions {
    bool showConditions = true;
    bool showTimers = true;
    bool showGlobals = true;
    size_t maxLabels = 4; // labels per merged edge before "+N more"
};
std::string graphDot(const PlanModel &m, const GraphOptions &o = {});
std::string graphMermaid(const PlanModel &m, const GraphOptions &o = {});
// Cross-plan graph: plans as nodes (or clusters of behaviours when `detailed`), CHANGE_OF_PLAN edges.
std::string graphDotAll(const std::vector<PlanModel> &plans, bool detailed, const GraphOptions &o = {});
std::string graphMermaidAll(const std::vector<PlanModel> &plans, const GraphOptions &o = {});

// =============================================================================================
// Inspect / lint
// =============================================================================================
struct Issue {
    enum class Severity { Info, Warning, Error };
    Severity severity;
    std::string path;
    std::string message;
};
// Structural lint against the conventional statement arities (grammar.md §4/§5.2) and the
// runtime's acceptance rules (Behavior::setup / PlanObject::execute). The same rules are meant
// for `adlibc --strict`. `words` (optional) adds token-level checks (trailing words after END_PLAN,
// bit-15 counts).
std::vector<Issue> lintPlan(const PlanTree &tree, const SymbolTable *syms,
                            const std::vector<uint32_t> *words = nullptr);
// Ids not present in the vocabulary (unknown ACF/DCF/MSG/COB/AGD/BEH/SET/PRN).
std::vector<Issue> checkIds(const PlanTree &tree, const SymbolTable &syms);
// Behaviours not reachable from behaviour 0 (plus `extraRoots`, e.g. other plans' CHANGE_OF_PLAN
// targets into this plan).
std::vector<int> unreachableBehaviours(const PlanModel &m, const std::vector<int> &extraRoots = {});
// Human-readable report: behaviours, interactors, node counts, symbol usage, issues.
std::string inspectReport(const PlanTree &tree, const PlanModel &m, const SymbolTable *syms,
                          const std::vector<uint32_t> *words, const std::vector<int> &extraRoots,
                          int *errors = nullptr);

// =============================================================================================
// Compare
// =============================================================================================
// Structural diff; returns the number of differences, report in `out`.
int comparePlans(const PlanTree &a, const PlanTree &b, const SymbolTable *syms, std::string &out);

// =============================================================================================
// Trace sink
// =============================================================================================
// Filter: comma-separated clauses, alternatives with '|', '*' wildcards, case-insensitive:
//   obj=<name|index>  plan=<P>  beh=<B>  msg=<M>  acf=<A>  dcf=<D>  kind=<K>  tick=<a>..<b>
// Kinds: MSG COLL DCF ACF CHANGE NEXT PLAN AGENDA TIMER PARAM (default: all but NEXT, PARAM).
// "all" (or "", "1") = everything with the default kinds; "kind=*" = every kind.
// msg= keeps a MSG line and the events that follow it for that object in the same tick.
struct TraceFilter {
    std::vector<std::string> obj, plan, beh, msg, acf, dcf, kind;
    int64_t tickMin = INT64_MIN, tickMax = INT64_MAX;
    bool parse(const std::string &spec, std::string *err = nullptr);
    bool kindEnabled(const std::string &k) const;
    static bool matchAny(const std::vector<std::string> &pats, const std::string &s);
};

class Tracer {
public:
    explicit Tracer(std::shared_ptr<const Vocabulary> vocab = nullptr) : vocab_(std::move(vocab)) {}
    TraceFilter filter;
    std::function<void(const std::string &)> sink;    // default: stderr
    std::function<int64_t(PlanObject &)> tick;        // default: scene clock
    std::function<std::string(PlanObject &)> objectName; // default: index
    // DO_ACTION arguments printed as message names: (action short name, child index).
    std::set<std::pair<std::string, int>> messageArgs{{"SendMessage", 2}};
    // Installs this tracer as reg.traceEvent (chaining any previous hook). The registry keeps a
    // shared_ptr, so the tracer lives as long as the hook.
    static std::shared_ptr<Tracer> attach(Registry &reg, const std::string &filterSpec, std::string *err = nullptr);
    void onEvent(PlanObject &o, const TraceEvent &ev);
    // "MSG Coin data=0 from=1" etc. (no prefix); kind token in `kind`.
    std::string describe(PlanObject &o, const TraceEvent &ev, std::string &kind) const;
    size_t lines() const { return lines_; }

private:
    std::string sym(Role r, int id) const;
    std::shared_ptr<const Vocabulary> vocab_;
    std::map<const PlanObject *, std::pair<int64_t, bool>> msgCtx_; // object -> (tick, in matching msg)
    size_t lines_ = 0;
};
// Applies a filter to trace lines already printed in the Tracer format (for `adlib trace`).
bool traceLineMatches(const TraceFilter &f, const std::string &line, std::map<std::string, bool> &ctx);

} // namespace adlib::tools
