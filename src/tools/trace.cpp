// adlib-tools: run-time trace sink for the libadlib runtime (Registry::traceEvent).
//
//   [tick 12] obj=machine(0) plan=VENDING beh=Idle MSG Coin data=0 from=1
//   [tick 12] obj=machine(0) plan=VENDING beh=Idle ACF AddCredit()
//   [tick 12] obj=machine(0) plan=VENDING beh=Idle CHANGE Idle -> HasCredit
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "adlib/tools.h"

namespace adlib::tools {

namespace {

std::string lower(std::string s) {
    for (char &c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

bool glob(const char *p, const char *s) {
    if (!*p) return !*s;
    if (*p == '*') return glob(p + 1, s) || (*s && glob(p, s + 1));
    return *s && std::tolower(uint8_t(*p)) == std::tolower(uint8_t(*s)) && glob(p + 1, s + 1);
}

std::vector<std::string> split(const std::string &s, char c) {
    std::vector<std::string> out;
    std::string cur;
    for (char x : s) {
        if (x == c) { out.push_back(cur); cur.clear(); }
        else cur += x;
    }
    out.push_back(cur);
    return out;
}

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

const char *kDefaultKinds[] = {"MSG", "COLL", "DCF", "ACF", "CHANGE", "PLAN", "AGENDA", "TIMER"};

} // namespace

bool TraceFilter::matchAny(const std::vector<std::string> &pats, const std::string &s) {
    if (pats.empty()) return true;
    for (auto &p : pats)
        if (glob(p.c_str(), s.c_str())) return true;
    return false;
}

bool TraceFilter::kindEnabled(const std::string &k) const {
    if (kind.empty()) {
        for (const char *d : kDefaultKinds)
            if (k == d) return true;
        return false;
    }
    return matchAny(kind, k);
}

bool TraceFilter::parse(const std::string &spec, std::string *err) {
    *this = TraceFilter{};
    std::string s = trim(spec);
    if (s.empty() || s == "all" || s == "1" || s == "on") return true;
    for (const std::string &clause0 : split(s, ',')) {
        std::string clause = trim(clause0);
        if (clause.empty()) continue;
        size_t eq = clause.find('=');
        if (eq == std::string::npos) {
            if (err) *err = "trace filter clause without '=': " + clause;
            return false;
        }
        std::string key = lower(trim(clause.substr(0, eq))), val = trim(clause.substr(eq + 1));
        std::vector<std::string> alts = split(val, '|');
        if (key == "tick") {
            size_t dd = val.find("..");
            try {
                if (dd == std::string::npos) tickMin = tickMax = std::stoll(val);
                else {
                    if (dd > 0) tickMin = std::stoll(val.substr(0, dd));
                    if (dd + 2 < val.size()) tickMax = std::stoll(val.substr(dd + 2));
                }
            } catch (...) {
                if (err) *err = "bad tick range: " + val;
                return false;
            }
            continue;
        }
        std::vector<std::string> *dst = key == "obj" || key == "object" ? &obj : key == "plan" ? &plan : key == "beh" || key == "behaviour" || key == "behavior" ? &beh
                                      : key == "msg" ? &msg : key == "acf" || key == "action" ? &acf : key == "dcf" || key == "decision" ? &dcf
                                      : key == "kind" ? &kind : nullptr;
        if (!dst) {
            if (err) *err = "unknown trace filter key: " + key;
            return false;
        }
        for (auto &a : alts) {
            std::string v = trim(a);
            if (key == "kind") for (char &c : v) c = char(std::toupper(uint8_t(c)));
            dst->push_back(v);
        }
    }
    return true;
}

std::string Tracer::sym(Role r, int id) const {
    if (vocab_) {
        const char *n = vocab_->name(r, id);
        if (n && *n) return Namespace::stripPrefix(n);
    }
    return std::to_string(id);
}

std::string Tracer::describe(PlanObject &o, const TraceEvent &ev, std::string &kind) const {
    using K = TraceEvent::Kind;
    std::ostringstream s;
    auto value = [&](uint32_t v, const std::string &hintNs) -> std::string {
        if ((v >> 16) == kTagPronoun) return sym(Role::Pronoun, int(v & 0xFFFF));
        switch (v) {
        case kMessageData: return "_MESSAGE_DATA_";
        case kBroadcast: return "_BROADCAST_";
        case kStraightAhead: return "_STRAIGHTAHEAD_";
        default: break;
        }
        int32_t i = int32_t(v);
        if (hintNs == "msg") return sym(Role::Message, i);
        if (i > -0x100000 && i < 0x100000) return std::to_string(i);
        return formatFloatLiteral(v);
    };
    switch (ev.kind) {
    case K::Message:
        kind = "MSG";
        s << "MSG " << sym(Role::Message, ev.id) << " data=" << ev.a << " from=" << ev.b;
        break;
    case K::Collision:
        kind = "COLL";
        s << "COLL " << sym(Role::CollisionObject, int(ev.a)) << "/" << sym(Role::CollisionObject, int(ev.b));
        break;
    case K::Action: {
        kind = "ACF";
        std::string a = sym(Role::Action, ev.id);
        s << "ACF " << a << "(";
        if (ev.node)
            for (size_t i = 1; i < ev.node->args.size(); ++i) {
                const Arg &x = ev.node->args[i];
                if (i > 1) s << ", ";
                if (x.node) s << "{" << (nodeIdName(x.node->id) ? nodeIdName(x.node->id) : "?") << "}";
                else if (x.kind == Arg::Kind::String) s << "\"" << x.str << "\"";
                else s << value(x.u, messageArgs.count({a, int(i)}) ? "msg" : "");
            }
        s << ")";
        break;
    }
    case K::Decision:
        kind = "DCF";
        s << "DCF " << sym(Role::Decision, ev.id) << " -> " << ev.a << (ev.b ? " (replay)" : "");
        break;
    case K::BehaviorParam:
        kind = "PARAM";
        s << "PARAM beh " << sym(Role::BehaviorParam, ev.id);
        break;
    case K::InteractorParam:
        kind = "PARAM";
        s << "PARAM int " << sym(Role::InteractorParam, ev.id);
        break;
    case K::Change:
        kind = "CHANGE";
        s << "CHANGE " << ev.text;
        break;
    case K::ChangeRequest: {
        kind = "NEXT";
        uint32_t v = uint32_t(ev.a);
        s << "NEXT ";
        if (v == kNoBehChange) s << "_NO_BEH_CHANGE_";
        else if (v == kPreviousBeh) s << "_PREVIOUS_BEH_";
        else {
            PlanInstance *p = o.active();
            if (p && !p->behaviors.empty()) s << p->behaviors[size_t(uint16_t(v)) % p->behaviors.size()]->name;
            else s << v;
        }
        break;
    }
    case K::PlanChange: {
        kind = "PLAN";
        s << "PLAN request ";
        if (ev.a >= 0 && size_t(ev.a) < o.planCount()) s << o.plan(size_t(ev.a)).name;
        else s << ev.a;
        s << " beh #" << ev.b;
        break;
    }
    case K::PlanActivated:
        kind = "PLAN";
        s << "PLAN -> " << (ev.id >= 0 && size_t(ev.id) < o.planCount() ? o.plan(size_t(ev.id)).name : std::to_string(ev.id));
        break;
    case K::AgendaAdd:
        kind = "AGENDA";
        s << "AGENDA + " << sym(Role::Agenda, ev.id);
        break;
    case K::AgendaRemove:
        kind = "AGENDA";
        s << "AGENDA - " << sym(Role::Agenda, ev.id);
        break;
    case K::TimerSet:
        kind = "TIMER";
        s << "TIMER set " << ev.a << " " << sym(Role::Message, ev.id) << " -> " << value(uint32_t(ev.b), "");
        break;
    case K::TimerFire:
        kind = "TIMER";
        s << "TIMER fire " << sym(Role::Message, ev.id) << " -> " << ev.b;
        break;
    }
    return s.str();
}

void Tracer::onEvent(PlanObject &o, const TraceEvent &ev) {
    int64_t t = tick ? tick(o) : int64_t(o.scene().clock);
    if (t < filter.tickMin || t > filter.tickMax) return;
    std::string kind;
    std::string body = describe(o, ev, kind);
    if (!filter.kindEnabled(kind)) return;
    std::string oname = objectName ? objectName(o) : std::string();
    std::string oid = std::to_string(o.index());
    if (!filter.obj.empty() && !TraceFilter::matchAny(filter.obj, oid) && (oname.empty() || !TraceFilter::matchAny(filter.obj, oname))) return;
    PlanInstance *p = o.active();
    std::string plan = p ? p->name : "-";
    std::string beh = p && p->current ? p->current->name : "-";
    if (!TraceFilter::matchAny(filter.plan, plan)) return;
    if (!filter.beh.empty()) {
        bool ok = TraceFilter::matchAny(filter.beh, beh);
        if (!ok && ev.kind == TraceEvent::Kind::Change) // either end of the change
            for (auto &part : {ev.text.substr(0, ev.text.find(" -> ")), ev.text.substr(ev.text.find(" -> ") + 4)})
                ok |= TraceFilter::matchAny(filter.beh, part.substr(0, part.find(" (")));
        if (!ok) return;
    }
    if (ev.kind == TraceEvent::Kind::Action && !filter.acf.empty() && !TraceFilter::matchAny(filter.acf, sym(Role::Action, ev.id))) return;
    if (ev.kind == TraceEvent::Kind::Decision && !filter.dcf.empty() && !TraceFilter::matchAny(filter.dcf, sym(Role::Decision, ev.id))) return;
    if (!filter.msg.empty()) {
        auto &ctx = msgCtx_[&o];
        if (ev.kind == TraceEvent::Kind::Message || ev.kind == TraceEvent::Kind::Collision) {
            ctx = {t, ev.kind == TraceEvent::Kind::Message && TraceFilter::matchAny(filter.msg, sym(Role::Message, ev.id))};
        } else if (ev.kind == TraceEvent::Kind::TimerSet || ev.kind == TraceEvent::Kind::TimerFire) {
            if (!TraceFilter::matchAny(filter.msg, sym(Role::Message, ev.id)) && !(ctx.first == t && ctx.second)) return;
        } else if (ctx.first != t) {
            ctx = {t, false};
        }
        if (ev.kind != TraceEvent::Kind::TimerSet && ev.kind != TraceEvent::Kind::TimerFire && !ctx.second) return;
    }
    std::ostringstream line;
    line << "[tick " << t << "] obj=";
    if (oname.empty()) line << oid;
    else line << oname << "(" << oid << ")";
    line << " plan=" << plan << " beh=" << beh << " " << body;
    ++lines_;
    if (sink) sink(line.str());
    else std::fprintf(stderr, "%s\n", line.str().c_str());
}

std::shared_ptr<Tracer> Tracer::attach(Registry &reg, const std::string &filterSpec, std::string *err) {
    auto t = std::make_shared<Tracer>();
    // The registry owns its vocabulary through a shared_ptr we cannot reach; keep a raw view.
    if (reg.vocabulary()) t->vocab_ = std::shared_ptr<const Vocabulary>(reg.vocabulary(), [](const Vocabulary *) {});
    if (!t->filter.parse(filterSpec, err)) return nullptr;
    TraceEventFn prev = reg.traceEvent;
    reg.traceEvent = [t, prev](PlanObject &o, const TraceEvent &ev) {
        if (prev) prev(o, ev);
        t->onEvent(o, ev);
    };
    return t;
}

bool traceLineMatches(const TraceFilter &f, const std::string &line, std::map<std::string, bool> &ctx) {
    // [tick N] obj=NAME(ID) plan=P beh=B KIND rest
    if (line.rfind("[tick ", 0) != 0) return false;
    size_t rb = line.find(']');
    if (rb == std::string::npos) return false;
    int64_t t = std::atoll(line.c_str() + 6);
    if (t < f.tickMin || t > f.tickMax) return false;
    std::istringstream is(line.substr(rb + 1));
    std::string o, p, b, kind;
    is >> o >> p >> b >> kind;
    auto field = [](const std::string &s, const char *k) { return s.rfind(k, 0) == 0 ? s.substr(std::strlen(k)) : std::string(); };
    std::string obj = field(o, "obj="), plan = field(p, "plan="), beh = field(b, "beh=");
    std::string rest;
    std::getline(is, rest);
    rest = trim(rest);
    if (!f.kindEnabled(kind)) return false;
    if (!f.obj.empty()) {
        std::string name = obj, id = obj;
        size_t lp = obj.find('(');
        if (lp != std::string::npos) { name = obj.substr(0, lp); id = obj.substr(lp + 1, obj.size() - lp - 2); }
        if (!TraceFilter::matchAny(f.obj, name) && !TraceFilter::matchAny(f.obj, id)) return false;
    }
    if (!TraceFilter::matchAny(f.plan, plan)) return false;
    if (!f.beh.empty()) {
        bool ok = TraceFilter::matchAny(f.beh, beh);
        if (!ok && kind == "CHANGE") {
            size_t ar = rest.find(" -> ");
            if (ar != std::string::npos) {
                std::string to = rest.substr(ar + 4);
                ok = TraceFilter::matchAny(f.beh, rest.substr(0, ar)) || TraceFilter::matchAny(f.beh, to.substr(0, to.find(" (")));
            }
        }
        if (!ok) return false;
    }
    std::string first = rest.substr(0, rest.find_first_of(" ("));
    if (kind == "ACF" && !TraceFilter::matchAny(f.acf, first)) return false;
    if (kind == "DCF" && !TraceFilter::matchAny(f.dcf, first)) return false;
    if (!f.msg.empty()) {
        std::string key = obj + "@" + std::to_string(t);
        if (kind == "MSG" || kind == "COLL") {
            ctx[key] = kind == "MSG" && TraceFilter::matchAny(f.msg, first);
        }
        if (kind == "TIMER") {
            std::istringstream ts(rest);
            std::string what, a, m;
            ts >> what >> a;
            m = what == "set" ? (ts >> m, m) : a;
            if (TraceFilter::matchAny(f.msg, m)) return true;
        }
        auto it = ctx.find(key);
        if (it == ctx.end() || !it->second) return false;
    }
    return true;
}

} // namespace adlib::tools
