// adlib-tools: behaviour/transition model of a plan, and its graphs (DOT, Mermaid).
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>

#include "adlib/tools.h"

namespace adlib::tools {

namespace {

std::string leafText(const Arg &a) {
    if (a.kind == Arg::Kind::String) return a.str;
    if (!a.str.empty()) return a.str;
    return std::to_string(a.i());
}

std::string upper(std::string s) {
    for (char &c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

struct Analyzer {
    const PlanTree &tree;
    const SymbolTable *syms;
    const std::map<std::string, std::vector<std::string>> *planBehs;
    PlanModel m;

    std::string name(Role r, int v) const { return syms ? syms->displayName(r, v) : std::to_string(v); }

    std::string valueName(uint32_t v, Role r) const {
        if ((v >> 16) == kTagPronoun) return "Prn:" + name(Role::Pronoun, int(v & 0xFFFF));
        if (v == kBroadcast) return "Broadcast";
        return name(r, int32_t(v));
    }

    std::string trigger(const Node &it) const {
        if (it.cls() == 0x00050000) {
            std::string pre = it.id == SET_LOC_MESSAGE_INTERACTOR ? "LOCMSG " : it.id == SET_NET_MESSAGE_INTERACTOR ? "NETMSG " : "MSG ";
            return pre + valueName(it.leaf(0), Role::Message);
        }
        return std::string(it.id == SET_LOC_COLLISION_INTERACTOR ? "LOCCOLL " : "COLL ") +
               valueName(it.leaf(0), Role::CollisionObject) + "/" + valueName(it.leaf(1), Role::CollisionObject);
    }

    void addChange(int from, const Node &n, const std::string &trig, const std::string &cond, bool option,
                   const std::string &path) {
        Transition t;
        t.from = from;
        t.trigger = trig;
        t.cond = cond;
        t.viaOption = option;
        t.path = path;
        if (n.id == CHANGE_TO_BEHAVIOR) {
            uint32_t v = n.leaf(0, kNoBehChange);
            if (v == kNoBehChange) return;
            if (v == kPreviousBeh) {
                t.kind = Transition::Kind::Previous;
            } else {
                t.to = int(v);
                size_t nb = m.behaviours.size();
                if (nb) {
                    size_t tgt = size_t(uint16_t(v)) % nb; // runtime wraps
                    t.to = int(tgt);
                    t.toName = m.behaviours[tgt].name;
                }
            }
        } else { // CHANGE_OF_PLAN
            t.kind = Transition::Kind::Plan;
            t.plan = n.args.size() > 0 ? leafText(n.args[0]) : "?";
            t.to = n.args.size() > 1 ? n.args[1].i() : 0;
            if (planBehs) {
                auto it = planBehs->find(upper(t.plan));
                if (it != planBehs->end() && t.to >= 0 && size_t(t.to) < it->second.size()) t.toName = it->second[size_t(t.to)];
            }
        }
        m.transitions.push_back(std::move(t));
    }

    // Walk an action list (Enable / Block / branch).
    void actions(int beh, const std::vector<Arg> &list, size_t first, const std::string &trig,
                 const std::string &cond, const std::string &path) {
        for (size_t k = first; k < list.size(); ++k) {
            const Node *n = list[k].node.get();
            if (!n) continue;
            action(beh, *n, trig, cond, path + "/" + std::to_string(k));
        }
    }

    void action(int beh, const Node &n, const std::string &trig, const std::string &cond, const std::string &path) {
        const char *kw = nodeIdName(n.id);
        std::string p = path + ":" + (kw ? kw : "?");
        switch (n.id) {
        case CHANGE_TO_BEHAVIOR:
        case CHANGE_OF_PLAN: addChange(beh, n, trig, cond, false, p); break;
        case BLOCK:
        case ENABLE: actions(beh, n.args, 0, trig, cond, p); break;
        case DECIDE_BY_AMONG:
        case DECIDE_BY_WITH_AMONG: {
            size_t first = n.id == DECIDE_BY_AMONG ? 1 : 2;
            std::string d = name(Role::Decision, int16_t(n.leaf(0)));
            for (size_t k = first; k < n.args.size(); ++k) {
                if (!n.args[k].node) continue;
                std::string c = cond + (cond.empty() ? "" : ", ") + d + "=" + std::to_string(k - 1);
                action(beh, *n.args[k].node, trig, c, p + "/" + std::to_string(k));
            }
            break;
        }
        case SET_TIMEOUTMSG: {
            Timer t;
            t.beh = beh;
            t.trigger = trig;
            t.delay = n.args.size() > 0 ? n.args[0].i() : 0;
            t.msg = n.args.size() > 1 ? name(Role::Message, n.args[1].i()) : "?";
            t.recipient = n.args.size() > 2 ? valueName(n.args[2].u, Role::Object) : "-";
            m.timers.push_back(t);
            break;
        }
        default: break;
        }
    }

    void interactor(int beh, const Node &it, const std::string &path) {
        std::string trig = trigger(it);
        if (beh >= 0) m.behaviours[size_t(beh)].triggers.push_back(trig);
        else m.globalTriggers.push_back(trig);
        size_t first = it.cls() == 0x00050000 ? 1 : 2;
        for (size_t k = first; k < it.args.size(); ++k) {
            const Node *c = it.args[k].node.get();
            if (!c) continue;
            std::string p = path + "/" + std::to_string(k);
            if (c->id == CHANGE_TO_BEHAVIOR || c->id == CHANGE_OF_PLAN) addChange(beh, *c, trig, "", true, p + ":CHANGE");
            else if (c->id == ENABLE) actions(beh, c->args, 0, trig, "", p + ":Enable");
        }
    }

    void run() {
        m.name = tree.name;
        for (auto &top : tree.top)
            if (top->id == DECLARE_BEHAVIOR) {
                BehaviourInfo b;
                b.index = int(m.behaviours.size());
                b.name = top->args.size() > 1 ? leafText(top->args[1]) : "?";
                b.type = top->args.empty() ? -1 : top->args[0].i();
                b.typeName = name(Role::Behavior, b.type);
                m.behaviours.push_back(b);
            }
        int bi = 0;
        for (size_t t = 0; t < tree.top.size(); ++t) {
            const Node &top = *tree.top[t];
            std::string path = "/" + std::to_string(t);
            if (top.id == GLOBAL_INTERACTORS) {
                for (size_t k = 0; k < top.args.size(); ++k)
                    if (top.args[k].node && (top.args[k].node->cls() == 0x00050000 || top.args[k].node->cls() == 0x00030000))
                        interactor(-1, *top.args[k].node, path + ":GLOBAL_INTERACTORS/" + std::to_string(k));
            } else if (top.id == DECLARE_BEHAVIOR) {
                BehaviourInfo &b = m.behaviours[size_t(bi)];
                std::string bp = path + ":\"" + b.name + "\"";
                for (size_t k = 2; k < top.args.size(); ++k) {
                    const Node *c = top.args[k].node.get();
                    if (!c) continue;
                    if (c->cls() == 0x00050000 || c->cls() == 0x00030000) interactor(bi, *c, bp + "/" + std::to_string(k));
                    else if (c->id == SET_BEHAVIOR_PARAM) b.params++;
                }
                b.fallthrough = b.triggers.empty();
                ++bi;
            }
        }
    }
};

std::string dotEsc(const std::string &s) {
    std::string r;
    for (char c : s) {
        if (c == '"' || c == '\\') r += '\\';
        if (c == '\n') { r += "\\n"; continue; }
        r += c;
    }
    return r;
}

std::string mmdEsc(const std::string &s) {
    std::string r;
    for (char c : s) {
        if (c == '"') r += "#quot;";
        else if (c == '\n') r += "<br/>";
        else if (c == '<') r += "#lt;";
        else if (c == '>') r += "#gt;";
        else r += c;
    }
    return r;
}

std::string edgeLabel(const Transition &t, const GraphOptions &o) {
    std::string l = t.trigger;
    if (o.showConditions && !t.cond.empty()) l += " [" + t.cond + "]";
    if (!t.viaOption) l += " *";
    return l;
}

struct Edge { std::string from, to; std::vector<std::string> labels; std::string style; };

struct GraphBuild {
    std::vector<std::pair<std::string, std::string>> nodes; // id, label
    std::map<std::string, std::string> nodeShape;
    std::vector<Edge> edges;

    void edge(const std::string &f, const std::string &t, const std::string &label, const std::string &style = "") {
        for (auto &e : edges)
            if (e.from == f && e.to == t && e.style == style) {
                if (std::find(e.labels.begin(), e.labels.end(), label) == e.labels.end()) e.labels.push_back(label);
                return;
            }
        edges.push_back({f, t, {label}, style});
    }
    static std::string joined(const Edge &e, const GraphOptions &o) {
        std::string s;
        size_t n = std::min(e.labels.size(), o.maxLabels);
        for (size_t i = 0; i < n; ++i) s += (i ? "\n" : "") + e.labels[i];
        if (e.labels.size() > n) s += "\n+" + std::to_string(e.labels.size() - n) + " more";
        return s;
    }
};

std::string sanitizeId(const std::string &s) {
    std::string r;
    for (char c : s) r += std::isalnum(uint8_t(c)) ? c : '_';
    return r;
}

GraphBuild buildPlanGraph(const PlanModel &m, const GraphOptions &o, const std::string &pfx) {
    GraphBuild g;
    std::map<int, std::vector<std::string>> timers;
    for (auto &t : m.timers) {
        std::string s = "timer " + std::to_string(t.delay) + " " + t.msg + (t.recipient == "Prn:Me" ? "" : " -> " + t.recipient);
        if (std::find(timers[t.beh].begin(), timers[t.beh].end(), s) == timers[t.beh].end()) timers[t.beh].push_back(s);
    }
    for (auto &b : m.behaviours) {
        std::string l = "#" + std::to_string(b.index) + " " + b.name + "\n(" + b.typeName + ")";
        if (b.fallthrough) l += "\nno interactors";
        if (o.showTimers)
            for (auto &t : timers[b.index]) l += "\n" + t;
        g.nodes.push_back({pfx + "b" + std::to_string(b.index), l});
        if (b.index == 0) g.nodeShape[pfx + "b0"] = "start";
    }
    bool needGlobal = false, needPrev = false;
    for (auto &t : m.transitions) {
        if (t.from < 0 && !o.showGlobals) continue;
        std::string f = t.from < 0 ? pfx + "global" : pfx + "b" + std::to_string(t.from);
        needGlobal |= t.from < 0;
        std::string to;
        if (t.kind == Transition::Kind::Behaviour) to = pfx + "b" + std::to_string(t.to);
        else if (t.kind == Transition::Kind::Previous) { to = pfx + "prev"; needPrev = true; }
        else {
            to = "plan_" + sanitizeId(upper(t.plan)) + "_" + std::to_string(t.to);
            bool have = false;
            for (auto &n : g.nodes) have |= n.first == to;
            if (!have) {
                g.nodes.push_back({to, upper(t.plan) + ":" + (t.toName.empty() ? "#" + std::to_string(t.to) : t.toName)});
                g.nodeShape[to] = "plan";
            }
        }
        g.edge(f, to, edgeLabel(t, o), t.kind == Transition::Kind::Plan ? "plan" : "");
    }
    for (auto &b : m.behaviours)
        if (b.fallthrough && size_t(b.index + 1) < m.behaviours.size())
            g.edge(pfx + "b" + std::to_string(b.index), pfx + "b" + std::to_string(b.index + 1), "fallthrough", "fall");
    if (needGlobal) {
        std::string l = "GLOBAL_INTERACTORS\n(any behaviour)";
        if (o.showTimers)
            for (auto &t : timers[-1]) l += "\n" + t;
        g.nodes.insert(g.nodes.begin(), {pfx + "global", l});
        g.nodeShape[pfx + "global"] = "global";
    }
    if (needPrev) {
        g.nodes.push_back({pfx + "prev", "_PREVIOUS_BEH_"});
        g.nodeShape[pfx + "prev"] = "prev";
    }
    return g;
}

void emitDot(std::ostringstream &s, const GraphBuild &g, const GraphOptions &o, const std::string &ind) {
    for (auto &[id, label] : g.nodes) {
        auto sh = g.nodeShape.find(id);
        std::string attr;
        if (sh != g.nodeShape.end()) {
            if (sh->second == "start") attr = ", penwidth=2";
            else if (sh->second == "global") attr = ", shape=note, style=filled, fillcolor=\"#eeeeee\"";
            else if (sh->second == "plan") attr = ", shape=component, style=filled, fillcolor=\"#ffe9c6\"";
            else if (sh->second == "prev") attr = ", shape=ellipse, style=dashed";
        }
        s << ind << id << " [label=\"" << dotEsc(label) << "\"" << attr << "];\n";
    }
    for (auto &e : g.edges) {
        std::string attr;
        if (e.style == "plan") attr = ", color=\"#c06000\", style=bold";
        if (e.style == "fall") attr = ", style=dotted";
        if (e.from.find("global") != std::string::npos) attr += ", color=\"#607080\"";
        s << ind << e.from << " -> " << e.to << " [label=\"" << dotEsc(GraphBuild::joined(e, o)) << "\"" << attr << "];\n";
    }
}

void emitMermaid(std::ostringstream &s, const GraphBuild &g, const GraphOptions &o, const std::string &ind) {
    for (auto &[id, label] : g.nodes) {
        auto sh = g.nodeShape.find(id);
        std::string open = "[\"", close = "\"]";
        if (sh != g.nodeShape.end()) {
            if (sh->second == "global") { open = "[/\""; close = "\"/]"; }
            else if (sh->second == "plan") { open = "[[\""; close = "\"]]"; }
            else if (sh->second == "prev") { open = "((\""; close = "\"))"; }
            else if (sh->second == "start") { open = "([\""; close = "\"])"; }
        }
        s << ind << id << open << mmdEsc(label) << close << "\n";
    }
    for (auto &e : g.edges) {
        std::string arrow = e.style == "fall" ? "-.->" : e.style == "plan" ? "==>" : "-->";
        s << ind << e.from << " " << arrow << "|\"" << mmdEsc(GraphBuild::joined(e, o)) << "\"| " << e.to << "\n";
    }
}

} // namespace

PlanModel analyze(const PlanTree &tree, const SymbolTable *syms,
                  const std::map<std::string, std::vector<std::string>> *planBehaviours) {
    Analyzer a{tree, syms, planBehaviours, {}};
    a.run();
    return std::move(a.m);
}

std::string graphDot(const PlanModel &m, const GraphOptions &o) {
    std::ostringstream s;
    s << "// " << m.name << ": behaviour-transition graph (adlib graph). Edge labels: trigger [decision path];\n"
      << "// '*' = CHANGE inside the Enable list (vs. the interactor's own CHANGE_TO_BEHAVIOR option).\n";
    s << "digraph \"" << dotEsc(m.name) << "\" {\n  rankdir=LR;\n  labelloc=t;\n  label=\"" << dotEsc(m.name)
      << "\";\n  node [shape=box, style=rounded, fontname=\"Helvetica\", fontsize=10];\n"
      << "  edge [fontname=\"Helvetica\", fontsize=8];\n";
    emitDot(s, buildPlanGraph(m, o, ""), o, "  ");
    s << "}\n";
    return s.str();
}

std::string graphMermaid(const PlanModel &m, const GraphOptions &o) {
    std::ostringstream s;
    s << "---\ntitle: " << m.name << "\n---\nflowchart LR\n";
    emitMermaid(s, buildPlanGraph(m, o, ""), o, "  ");
    return s.str();
}

namespace {
struct PlanEdge { std::string from, to; std::vector<std::string> labels; };
std::vector<PlanEdge> planEdges(const std::vector<PlanModel> &plans) {
    std::vector<PlanEdge> out;
    for (auto &m : plans)
        for (auto &t : m.transitions) {
            if (t.kind != Transition::Kind::Plan) continue;
            std::string from = t.from < 0 ? "GLOBAL" : m.behaviours[size_t(t.from)].name;
            std::string l = from + ": " + t.trigger + " -> " + (t.toName.empty() ? "#" + std::to_string(t.to) : t.toName);
            std::string to = upper(t.plan);
            auto it = std::find_if(out.begin(), out.end(), [&](auto &e) { return e.from == m.name && e.to == to; });
            if (it == out.end()) out.push_back({m.name, to, {l}});
            else if (std::find(it->labels.begin(), it->labels.end(), l) == it->labels.end()) it->labels.push_back(l);
        }
    return out;
}
} // namespace

std::string graphDotAll(const std::vector<PlanModel> &plans, bool detailed, const GraphOptions &o) {
    std::ostringstream s;
    s << "// cross-plan graph (adlib graph --all): CHANGE_OF_PLAN edges\n";
    s << "digraph plans {\n  rankdir=LR;\n  node [shape=box, style=rounded, fontname=\"Helvetica\", fontsize=10];\n"
      << "  edge [fontname=\"Helvetica\", fontsize=8];\n";
    if (!detailed) {
        std::set<std::string> referenced;
        auto edges = planEdges(plans);
        for (auto &e : edges) referenced.insert(e.to), referenced.insert(e.from);
        for (auto &m : plans)
            s << "  " << sanitizeId(m.name) << " [label=\"" << dotEsc(m.name + "\n" + std::to_string(m.behaviours.size()) + " behaviours") << "\""
              << (referenced.count(m.name) ? "" : ", style=\"rounded,dashed\"") << "];\n";
        for (auto &e : edges) {
            std::string l;
            size_t n = std::min(e.labels.size(), o.maxLabels);
            for (size_t i = 0; i < n; ++i) l += (i ? "\n" : "") + e.labels[i];
            if (e.labels.size() > n) l += "\n+" + std::to_string(e.labels.size() - n) + " more";
            s << "  " << sanitizeId(e.from) << " -> " << sanitizeId(e.to) << " [label=\"" << dotEsc(l) << "\"];\n";
        }
    } else {
        GraphOptions oo = o;
        for (auto &m : plans) {
            std::string pfx = sanitizeId(m.name) + "_";
            GraphBuild g = buildPlanGraph(m, oo, pfx);
            // plan targets become edges into the other cluster's behaviour node
            for (auto &e : g.edges)
                if (e.style == "plan") {
                    size_t u = e.to.rfind('_');
                    std::string plan = e.to.substr(5, u - 5);
                    e.to = plan + "_b" + e.to.substr(u + 1);
                }
            g.nodes.erase(std::remove_if(g.nodes.begin(), g.nodes.end(), [&](auto &n) { return n.first.rfind("plan_", 0) == 0; }),
                          g.nodes.end());
            s << "  subgraph cluster_" << sanitizeId(m.name) << " {\n    label=\"" << dotEsc(m.name) << "\";\n";
            emitDot(s, g, oo, "    ");
            s << "  }\n";
        }
    }
    s << "}\n";
    return s.str();
}

std::string graphMermaidAll(const std::vector<PlanModel> &plans, const GraphOptions &o) {
    std::ostringstream s;
    s << "---\ntitle: cross-plan graph\n---\nflowchart LR\n";
    for (auto &m : plans) s << "  " << sanitizeId(m.name) << "[\"" << m.name << "\"]\n";
    for (auto &e : planEdges(plans)) {
        std::string l;
        size_t n = std::min(e.labels.size(), o.maxLabels);
        for (size_t i = 0; i < n; ++i) l += (i ? "\n" : "") + e.labels[i];
        if (e.labels.size() > n) l += "\n+" + std::to_string(e.labels.size() - n) + " more";
        s << "  " << sanitizeId(e.from) << " ==>|\"" << mmdEsc(l) << "\"| " << sanitizeId(e.to) << "\n";
    }
    return s.str();
}

} // namespace adlib::tools
