#include "adlib/pbn.h"

#include <cstdio>

namespace adlib {

const char *nodeIdName(uint32_t id) {
    switch (id) {
    case SET_COLLISION_INTERACTOR: return "SET_COLLISION_INTERACTOR";
    case SET_LOC_COLLISION_INTERACTOR: return "SET_LOC_COLLISION_INTERACTOR";
    case SET_MESSAGE_INTERACTOR: return "SET_MESSAGE_INTERACTOR";
    case SET_LOC_MESSAGE_INTERACTOR: return "SET_LOC_MESSAGE_INTERACTOR";
    case SET_NET_MESSAGE_INTERACTOR: return "SET_NET_MESSAGE_INTERACTOR";
    case SET_DEBUG: return "SET_DEBUG";
    case SET_TRACE: return "SET_TRACE";
    case DECLARE_BEHAVIOR: return "DECLARE_BEHAVIOR";
    case CHANGE_TO_BEHAVIOR: return "CHANGE_TO_BEHAVIOR";
    case SET_BEHAVIOR_PARAM: return "SET_BEHAVIOR_PARAM";
    case CHANGE_OF_PLAN: return "CHANGE_OF_PLAN";
    case DECLARE_LOCAL_REALS: return "DECLARE_LOCAL_REALS";
    case DECIDE_BY_AMONG: return "DECIDE_BY_AMONG";
    case DECIDE_BY_WITH_AMONG: return "DECIDE_BY_WITH_AMONG";
    case DO_ACTION: return "DO_ACTION";
    case SET_INTERACTOR_PARAM: return "SET_INTERACTOR_PARAM";
    case DO_LOCAL_ACTION: return "DO_LOCAL_ACTION";
    case DO_NET_ACTION: return "DO_NET_ACTION";
    case ADD_AGENDA_ITEM: return "ADD_AGENDA_ITEM";
    case SET_TIMEOUTMSG: return "SET_TIMEOUTMSG";
    case REMOVE_AGENDA_ITEM: return "REMOVE_AGENDA_ITEM";
    case GLOBAL_INTERACTORS: return "GLOBAL_INTERACTORS";
    case DATA_BLOCK: return "DataBlock";
    case BLOCK: return "Block";
    case ENABLE: return "Enable";
    case END_PLAN: return "END_PLAN";
    default: return nullptr;
    }
}

bool readPbnWords(const std::vector<uint8_t> &file, std::vector<uint32_t> &words, std::string *err) {
    auto fail = [&](const char *m) { if (err) *err = m; return false; };
    if (file.size() < 4) return fail("Misread binary plan (no header)");
    uint32_t n;
    std::memcpy(&n, file.data(), 4);
    if (uint64_t(n) * 4 > file.size() - 4) return fail("Misread binary plan (short file)");
    words.resize(n);
    if (n) std::memcpy(words.data(), file.data() + 4, size_t(n) * 4);
    return true;
}

namespace {

struct TreeReader {
    const std::vector<uint32_t> &w;
    const LoadHooks &hooks;
    PlanTree &tree;
    size_t pos = 0;
    std::string err;

    bool need(size_t n) {
        if (pos + n > w.size()) { err = "token stream ends inside a statement"; return false; }
        return true;
    }

    // PlanNode_ReadTree 0x45b000. `cop` is the original's "inside CHANGE_OF_PLAN" flag (param_4).
    // Returns false on malformed input (the original would read past the buffer).
    bool read(Arg &out, bool &cop) {
        if (!need(1)) return false;
        uint32_t t = w[pos];
        if (t == kTokNode) {
            if (!need(2)) return false;
            auto n = std::make_unique<Node>();
            n->id = w[pos + 1];
            pos += 2;
            if (n->id != END_PLAN) {
                if (!need(1)) return false;
                n->rawCount = uint16_t(w[pos]);
                pos += 1;
                size_t cnt = n->rawCount & 0x7FFF;
                n->args.resize(cnt);
                for (size_t i = 0; i < cnt; ++i) {
                    if (n->cls() == CHANGE_OF_PLAN) {
                        if (n->variant() == 0) cop = true;
                    } else if (n->id == SET_INTERACTOR_PARAM && (i == 0 || n->args[0].u == 0)) {
                        // plan+0x38 counter; the original tests children[0] before it is read
                        // on the first iteration (uninitialised heap), we treat that as 0.
                        tree.nameSlots++;
                    }
                    if (n->id == CHANGE_OF_PLAN) tree.hasChangeOfPlan = true;
                    if (!read(n->args[i], cop)) return false;
                }
            }
            out.kind = Arg::Kind::Node;
            out.node = std::move(n);
            return true;
        }
        if (t == kTokString) {
            pos += 1;
            std::string s;
            while (true) {
                if (!need(1)) return false;
                uint32_t c = w[pos++];
                if (c == 0) break;
                if (s.size() < 31) s.push_back(char(c & 0xFF)); // original copies into char[32]
            }
            if (cop) {
                int idx = hooks.planIndex ? hooks.planIndex(s) : -1;
                out.kind = Arg::Kind::Leaf;
                out.u = uint32_t(int32_t(int16_t(idx)));
                out.str = s; // kept for diagnostics
                cop = false;
            } else {
                out.kind = Arg::Kind::String;
                out.str = s;
            }
            return true;
        }
        // leaf
        pos += 1;
        cop = false;
        out.kind = Arg::Kind::Leaf;
        if ((t >> 16) == kTagPronoun && hooks.pronoun)
            out.u = hooks.pronoun(uint16_t(t & 0xFFFF), t);
        else
            out.u = t;
        return true;
    }
};

} // namespace

bool buildPlanTree(const std::vector<uint32_t> &words, const LoadHooks &hooks, PlanTree &out,
                   std::string *err) {
    TreeReader r{words, hooks, out, 0, {}};
    out.top.clear();
    out.nameSlots = 0;
    bool cop = false;
    while (true) {
        Arg a;
        if (!r.read(a, cop)) { if (err) *err = r.err; return false; }
        if (!a.isNode()) {
            // "ERROR: No Instruction returned f..." (thrown by PlanInstance_LoadNodes)
            if (err) *err = "ERROR: No Instruction returned from plan node reader";
            return false;
        }
        bool end = a.node->id == END_PLAN;
        out.top.push_back(std::move(a.node));
        if (end) break;
    }
    if (r.pos != words.size() && err) *err = "trailing data after END_PLAN";
    return true;
}

static void dumpRec(std::string &s, const Node &n, int depth) {
    s.append(size_t(depth) * 2, ' ');
    const char *nm = nodeIdName(n.id);
    char buf[64];
    if (nm) s += nm; else { std::snprintf(buf, sizeof buf, "NODE_%08x", n.id); s += buf; }
    if (n.id == END_PLAN) { s += '\n'; return; }
    std::snprintf(buf, sizeof buf, "(%d)\n", int(n.args.size()));
    s += buf;
    for (auto &a : n.args) {
        if (a.isNode()) { dumpRec(s, *a.node, depth + 1); continue; }
        s.append(size_t(depth + 1) * 2, ' ');
        if (a.kind == Arg::Kind::String) s += "\"" + a.str + "\"";
        else if (a.tag() == kTagPronoun) { std::snprintf(buf, sizeof buf, "PRN:%u", a.u & 0xFFFF); s += buf; }
        else { std::snprintf(buf, sizeof buf, "0x%x", a.u); s += buf; if (!a.str.empty()) s += " /*plan " + a.str + "*/"; }
        s += '\n';
    }
}

std::string dumpNode(const Node &n, int depth) {
    std::string s;
    dumpRec(s, n, depth);
    return s;
}

} // namespace adlib
