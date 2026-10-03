// ADLIB compiled plan (.PBN): dword token stream <-> statement node tree.
//
// Mirrors LoadBinaryPlan 0x44f5f0 / ReadBinaryPlan 0x44f560 (file read), PlanInstance_LoadNodes
// 0x45ae30 (top-level loop) and PlanNode_ReadTree 0x45b000 (recursive node reader).
// See docs/language.md (format) and docs/host-api.md (runtime).
#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace adlib {

// ---- token tags ---------------------------------------------------------------------------
constexpr uint32_t kTokNode = 0xABCDEFFF;
constexpr uint32_t kTokString = 0xABCDEFFE;
constexpr uint16_t kTagPronoun = 0xFEDC;   // 0xFEDCxxxx: id_PRN_xxxx
constexpr uint16_t kTagLocalReal = 0xEDCB; // 0xEDCBxxxx: local real #xxxx
constexpr uint16_t kTagNameRef = 0xDCBA;   // 0xDCBAxxxx: interactor name slot #xxxx

// ---- special constants (compiler keywords) -----------------------------------------------
constexpr uint32_t kBroadcast = 0xFFFF;
constexpr uint32_t kStraightAhead = 0xABCDABCD;
constexpr uint32_t kTemporarily = 0xABCDABCC;
constexpr uint32_t kMessageData = 0xABCDABCE;
constexpr uint32_t kNoBehChange = 0xABCDABCB;
constexpr uint32_t kPreviousBeh = 0xABCDABCA;

// ---- node ids (hi16 = statement class, lo16 = variant) -----------------------------------
enum NodeId : uint32_t {
    SET_COLLISION_INTERACTOR = 0x00030000,
    SET_LOC_COLLISION_INTERACTOR = 0x00030001,
    SET_MESSAGE_INTERACTOR = 0x00050000,
    SET_LOC_MESSAGE_INTERACTOR = 0x00050001,
    SET_NET_MESSAGE_INTERACTOR = 0x00050002, // also SET_NET_COLLISION_INTERACTOR (sic)
    SET_DEBUG = 0x00060001,
    SET_TRACE = 0x00060002,
    DECLARE_BEHAVIOR = 0x00070000,
    CHANGE_TO_BEHAVIOR = 0x00070002,
    SET_BEHAVIOR_PARAM = 0x00070003,
    CHANGE_OF_PLAN = 0x00160000,
    DECLARE_LOCAL_REALS = 0x00160001,
    DECIDE_BY_AMONG = 0x00170000,
    DECIDE_BY_WITH_AMONG = 0x00170001,
    DO_ACTION = 0x00180000,
    SET_INTERACTOR_PARAM = 0x00180001,
    DO_LOCAL_ACTION = 0x00180002,
    DO_NET_ACTION = 0x00180003,
    ADD_AGENDA_ITEM = 0x001A0001,
    SET_TIMEOUTMSG = 0x001A0002,
    REMOVE_AGENDA_ITEM = 0x001A0003,
    GLOBAL_INTERACTORS = 0x001B0000,
    DATA_BLOCK = 0x20000000,
    BLOCK = 0x40000000,
    ENABLE = 0x80000000,
    END_PLAN = 0xFFFF0000,
};
constexpr uint32_t nodeClass(uint32_t id) { return id & 0xFFFF0000u; }
constexpr uint16_t nodeVariant(uint32_t id) { return uint16_t(id & 0xFFFF); }
const char *nodeIdName(uint32_t id); // keyword or nullptr

struct Node;

// One child slot. The original stores a raw u32 that is either a Node*, a char*, or a literal;
// consumers know which by position. We keep the kind explicit.
struct Arg {
    enum class Kind : uint8_t { Leaf, Node, String };
    Kind kind = Kind::Leaf;
    uint32_t u = 0;             // Leaf: raw dword (after load-time pronoun substitution)
    std::unique_ptr<Node> node; // Node
    std::string str;            // String (CHANGE_OF_PLAN targets become Leaf plan indices)

    bool isNode() const { return kind == Kind::Node; }
    bool isLeaf() const { return kind == Kind::Leaf; }
    int32_t i() const { return int32_t(u); }
    int16_t s() const { return int16_t(u & 0xFFFF); }
    float f() const { float v; std::memcpy(&v, &u, 4); return v; }
    uint16_t tag() const { return uint16_t(u >> 16); }
};

struct Node {
    uint32_t id = 0;
    uint16_t rawCount = 0; // as stored (bit 15 preserved; never set in shipped data)
    std::vector<Arg> args; // (rawCount & 0x7FFF) children

    uint32_t cls() const { return nodeClass(id); }
    uint16_t variant() const { return nodeVariant(id); }
    // The interpreter uses the signed 16-bit count field; equals args.size() for shipped data.
    int16_t count() const { return int16_t(args.size()); }
    const Node *child(size_t i) const { return i < args.size() ? args[i].node.get() : nullptr; }
    uint32_t leaf(size_t i, uint32_t def = 0) const { return i < args.size() ? args[i].u : def; }
};

// Load-time hooks (owner object context).
struct LoadHooks {
    // Load-time pronoun table 0x522AC8 (InitPronounResolverTable 0x4e1290). Returns the dword
    // stored in place of the 0xFEDCxxxx token. Runtime pronouns return the token unchanged.
    std::function<uint32_t(uint16_t prn, uint32_t token)> pronoun;
    // CGameObject vf20 (vtable+0x50): plan name -> index in the object's SetPlans list (-1 = none).
    std::function<int(const std::string &)> planIndex;
};

struct PlanTree {
    std::string name;
    std::vector<std::unique_ptr<Node>> top; // top-level statements, last is END_PLAN
    uint16_t nameSlots = 0;                 // plan+0x38 counter (SET_INTERACTOR_PARAM names)
    bool hasChangeOfPlan = false;
};

// ReadBinaryPlan: u32 n, then n dwords. Returns false on short/odd files.
bool readPbnWords(const std::vector<uint8_t> &file, std::vector<uint32_t> &words, std::string *err);
// PlanInstance_LoadNodes: read top-level nodes until END_PLAN.
bool buildPlanTree(const std::vector<uint32_t> &words, const LoadHooks &hooks, PlanTree &out,
                   std::string *err);

// Debug dump (one node per line, symbolic leaves).
std::string dumpNode(const Node &n, int depth = 0);

// =============================================================================================
// Writer (the back end of a plan compiler). Inverse of readPbnWords/buildPlanTree:
//   Node    -> 0xABCDEFFF, id, count, children...   (END_PLAN: 0xABCDEFFF, 0xFFFF0000)
//   String  -> 0xABCDEFFE, one char per dword, 0
//   Leaf    -> the dword; a Leaf carrying `str` (a CHANGE_OF_PLAN target resolved at load time)
//              is written back as its String, so load(identity hooks) -> write round-trips.
// rawCount is written when its low 15 bits equal the child count (preserving bit 15), else the
// child count. A missing trailing END_PLAN is appended by encodePlan.
void encodeNode(const Node &n, std::vector<uint32_t> &out);
std::vector<uint32_t> encodePlan(const std::vector<std::unique_ptr<Node>> &top);
std::vector<uint32_t> encodePlan(const PlanTree &tree);
std::vector<uint8_t> pbnFileBytes(const std::vector<uint32_t> &words); // u32 count + dwords (LE)
bool writePbnFile(const std::string &path, const std::vector<uint32_t> &words, std::string *err = nullptr);
bool readPbnFile(const std::string &path, std::vector<uint32_t> &words, std::string *err = nullptr);

// Structural dump of a raw token stream, one token per line with its offset (for compiler work:
// diff two PBNs token by token). Does not need hooks; pronouns and strings stay raw.
std::string dumpTokens(const std::vector<uint32_t> &words);

class Vocabulary;
// Plan dump with symbolic names where the argument type is known from the statement shape
// (DO_ACTION/DECIDE function ids, interactor message/COB ids, behaviour types, agenda ids,
// SET_*_PARAM ids, pronouns). `vocab` may be null (numbers only).
std::string dumpPlan(const PlanTree &tree, const Vocabulary *vocab);

// =============================================================================================
// Builder: hand-assemble plans in C++ (until the text compiler exists). Example:
//   using namespace adlib::build;
//   top.push_back(behavior(idBehType, "Idle", list(
//       onMessage(idMsgCoin, list(changeTo(1)), list(action(idAcfShow, args(lit(3))))))));
namespace build {
Arg lit(uint32_t v);                 // raw dword
Arg lit(int v);
Arg real(float v);                   // IEEE bits (the compiler's "1.5")
Arg str(const std::string &s);
Arg pronoun(uint16_t prn);           // 0xFEDC'prn
Arg node(std::unique_ptr<Node> n);
std::unique_ptr<Node> make(uint32_t id, std::vector<Arg> args);
// Statements.
std::unique_ptr<Node> behavior(int type, const std::string &name, std::vector<std::unique_ptr<Node>> items);
std::unique_ptr<Node> globals(std::vector<std::unique_ptr<Node>> interactors);
// Interactors: `opts` are CHANGE_TO_BEHAVIOR / SET_INTERACTOR_PARAM nodes, `actions` the Enable list.
std::unique_ptr<Node> onMessage(int msg, std::vector<std::unique_ptr<Node>> opts,
                                std::vector<std::unique_ptr<Node>> actions, uint32_t variant = 0);
std::unique_ptr<Node> onCollision(uint32_t cobMine, uint32_t cobOther, std::vector<std::unique_ptr<Node>> opts,
                                  std::vector<std::unique_ptr<Node>> actions, uint32_t variant = 0);
std::unique_ptr<Node> action(int acf, std::vector<Arg> args = {}, uint32_t variant = 0); // DO_[LOCAL_|NET_]ACTION
std::unique_ptr<Node> decide(int dcf, std::vector<std::unique_ptr<Node>> branches);      // DECIDE_BY_AMONG
std::unique_ptr<Node> decideWith(int dcf, std::vector<Arg> data, std::vector<std::unique_ptr<Node>> branches);
std::unique_ptr<Node> block(std::vector<std::unique_ptr<Node>> stmts);
std::unique_ptr<Node> changeTo(uint32_t behIndex);       // CHANGE_TO_BEHAVIOR (kPreviousBeh, kNoBehChange ok)
std::unique_ptr<Node> changePlan(const std::string &plan, int behIndex);
std::unique_ptr<Node> behaviorParam(int setId, std::vector<Arg> args = {});
std::unique_ptr<Node> interactorParam(int setId, std::vector<Arg> args = {});
std::unique_ptr<Node> addAgenda(int agd, std::vector<Arg> args = {});
std::unique_ptr<Node> removeAgenda(int agd);
std::unique_ptr<Node> timeout(int32_t delay, int msg, Arg recipient, std::optional<Arg> data = std::nullopt);
std::unique_ptr<Node> localReals(int n);
std::unique_ptr<Node> endPlan();
// Convenience: vector of nodes from a brace list of unique_ptrs (which cannot be copied).
template <class... T> std::vector<std::unique_ptr<Node>> list(T &&...n) {
    std::vector<std::unique_ptr<Node>> v;
    (v.push_back(std::forward<T>(n)), ...);
    return v;
}
template <class... T> std::vector<Arg> args(T &&...a) {
    std::vector<Arg> v;
    (v.push_back(std::forward<T>(a)), ...);
    return v;
}
} // namespace build

} // namespace adlib
