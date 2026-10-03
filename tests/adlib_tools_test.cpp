// adlib-tools unit tests (ctest: adlib_tools_test); no external data.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "adlib/adlib.h"
#include "adlib/tools.h"

using namespace adlib;
using namespace adlib::tools;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

static bool contains(const std::string &h, const std::string &n) { return h.find(n) != std::string::npos; }

static std::string readText(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

// Non-blank, non-comment lines, CR stripped.
static std::vector<std::string> codeLines(const std::string &t) {
    std::vector<std::string> out;
    std::istringstream is(t);
    std::string l;
    while (std::getline(is, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        size_t a = l.find_first_not_of(' ');
        if (a == std::string::npos || l.compare(a, 2, "//") == 0) continue;
        out.push_back(l);
    }
    return out;
}

struct Dummy : PlanHost {
    int16_t i;
    explicit Dummy(int16_t x) : i(x) {}
    int16_t objectIndex() const override { return i; }
};

static void testFloats() {
    CHECK(formatFloatLiteral(0x3F800000) == "1.0");
    CHECK(formatFloatLiteral(0x3FC00000) == "1.5");
    CHECK(formatFloatLiteral(0xBF800000) == "-1.0");
    CHECK(formatFloatLiteral(0x7FC00000) == "0x7fc00000"); // NaN: hex
}

static void testVending() {
    SymbolTable syms;
    std::string err;
    CHECK(loadSymbolsDir(syms, VENDING_DIR, &err));
    CHECK(syms.lookup("ID_ACF_ADDCREDIT") && syms.lookup("id_ACF_AddCredit")->value == 0);
    CHECK(syms.roleName(Role::Message, syms.vocab().id(Role::Message, "Coin")) == "id_MSG_Coin");

    // Plan built in code (independent of the example's generated files).
    const Vocabulary &V = syms.vocab();
    using namespace build;
    std::vector<std::unique_ptr<Node>> top;
    top.push_back(behavior(V.id(Role::Behavior, "Machine"), "Idle", list(
        onMessage(V.id(Role::Message, "Coin"), list(changeTo(1)), list(action(V.id(Role::Action, "AddCredit")))),
        onMessage(V.id(Role::Message, "Select"), {}, list(decide(V.id(Role::Decision, "EnoughCredit"), list(
            changeTo(kNoBehChange), changeTo(1))))))));
    top.push_back(behavior(V.id(Role::Behavior, "Machine"), "Has Credit", list(
        onMessage(V.id(Role::Message, "Refund"), {}, list(changeTo(0), timeout(100, V.id(Role::Message, "VendDone"), pronoun(0)))))));
    std::vector<uint32_t> words = encodePlan(top);

    std::string src;
    DecompileOptions o;
    o.planName = "TEST";
    CHECK(decompile(words, syms, o, src, &err));
    CHECK(contains(src, "DECLARE_BEHAVIOR 4 id_BEH_Machine \"Idle\""));
    CHECK(contains(src, "SET_MESSAGE_INTERACTOR 3 id_MSG_Coin"));
    CHECK(contains(src, "CHANGE_TO_BEHAVIOR 1 \"Has_Credit\""));
    CHECK(contains(src, "DECIDE_BY_AMONG 3 id_DCF_EnoughCredit"));
    CHECK(contains(src, "SET_TIMEOUTMSG 3 100 id_MSG_VendDone id_PRN_Me"));
    CHECK(contains(src, "END_PLAN"));
    o.annotate = true;
    CHECK(decompile(words, syms, o, src, &err) && contains(src, "\"Has_Credit\"  // #1"));

    // A string the compiler cannot express ('_') is an error, not silent damage.
    std::vector<std::unique_ptr<Node>> bad;
    bad.push_back(behavior(0, "bad_name", list(onMessage(0, list(changeTo(0)), {}))));
    CHECK(!decompile(encodePlan(bad), syms, o, src, &err) && contains(err, "not expressible"));

    PlanTree tree;
    CHECK(buildPlanTree(words, LoadHooks{}, tree, &err));
    tree.name = "TEST";
    PlanModel m = analyze(tree, &syms);
    CHECK(m.behaviours.size() == 2);
    bool coin = false, sel = false, prevBack = false;
    for (auto &t : m.transitions) {
        coin |= t.from == 0 && t.to == 1 && t.trigger == "MSG Coin" && t.viaOption;
        sel |= t.from == 0 && t.to == 1 && t.cond == "EnoughCredit=1" && !t.viaOption;
        prevBack |= t.from == 1 && t.to == 0;
    }
    CHECK(coin && sel && prevBack);
    CHECK(m.timers.size() == 1 && m.timers[0].msg == "VendDone");
    std::string dot = graphDot(m), mmd = graphMermaid(m);
    CHECK(contains(dot, "b0 -> b1") && contains(dot, "MSG Coin"));
    CHECK(contains(mmd, "flowchart LR") && contains(mmd, "b0 -->"));
    CHECK(unreachableBehaviours(m).empty());

    auto issues = lintPlan(tree, &syms, &words);
    int errs = 0;
    for (auto &i : issues) errs += i.severity == Issue::Severity::Error;
    CHECK(errs == 0);
    CHECK(checkIds(tree, syms).empty());

    // lint: a value in an action list, an unknown action id
    std::vector<std::unique_ptr<Node>> lt;
    std::vector<Arg> en;
    en.push_back(lit(5));
    en.push_back(node(action(99)));
    lt.push_back(behavior(0, "X", list(onMessage(0, {}, {}))));
    lt[0]->args[2].node->args.push_back(node(make(ENABLE, std::move(en))));
    PlanTree lt2;
    CHECK(buildPlanTree(encodePlan(lt), LoadHooks{}, lt2, &err));
    issues = lintPlan(lt2, &syms);
    bool valueErr = false;
    for (auto &i : issues) valueErr |= i.severity == Issue::Severity::Error && contains(i.message, "value in an action list");
    CHECK(valueErr);
    auto ids = checkIds(lt2, syms);
    CHECK(!ids.empty() && contains(ids[0].message, "unknown action id 99"));

    // compare
    std::string out;
    CHECK(comparePlans(tree, tree, &syms, out) == 0);
    std::vector<std::unique_ptr<Node>> top2;
    top2.push_back(behavior(V.id(Role::Behavior, "Machine"), "Idle", list(
        onMessage(V.id(Role::Message, "Coin"), list(changeTo(0)), list(action(V.id(Role::Action, "AddCredit")))))));
    PlanTree t2;
    CHECK(buildPlanTree(encodePlan(top2), LoadHooks{}, t2, &err));
    int nd = comparePlans(tree, t2, &syms, out);
    CHECK(nd >= 2 && contains(out, "0:DECLARE_BEHAVIOR\"Idle\"/2:SET_MESSAGE_INTERACTOR(Coin)/1:CHANGE_TO_BEHAVIOR/0: value"));
    CHECK(contains(out, "removed DECLARE_BEHAVIOR"));

    // tracer on a live run
    auto vocab = syms.vocabPtr();
    Registry reg(vocab);
    reg.log = [](const std::string &) {};
    int credit = 0;
    reg.action("AddCredit", [&](ActionCtx &) { ++credit; });
    reg.decision("EnoughCredit", [&](ActionCtx &) -> uint16_t { return credit >= 1 ? 1 : 0; });
    reg.behavior("Machine", [] { return std::make_unique<Behavior>(); });
    std::vector<std::string> lines;
    auto tracer = Tracer::attach(reg, "all", &err);
    CHECK(tracer);
    tracer->sink = [&](const std::string &l) { lines.push_back(l); };
    tracer->objectName = [](PlanObject &) { return std::string("machine"); };
    PlanScene scene;
    Dummy host(0);
    PlanObject obj(scene, host, reg);
    scene.add(&obj);
    CHECK(obj.loadPlanFromWords("VENDING", words));
    obj.start(0, 0);
    scene.clock = 7;
    obj.sendMessage(V.id(Role::Message, "Coin"), 0, 1);
    scene.update();
    std::string all;
    for (auto &l : lines) all += l + "\n";
    CHECK(contains(all, "[tick 7] obj=machine(0) plan=VENDING beh=Idle MSG Coin data=0 from=1"));
    CHECK(contains(all, "beh=Idle ACF AddCredit()"));
    CHECK(contains(all, "CHANGE Idle -> Has Credit"));
    // filters
    lines.clear();
    CHECK(tracer->filter.parse("kind=CHANGE|TIMER, beh=Has*", &err));
    obj.sendMessage(V.id(Role::Message, "Refund"), 0, 1);
    scene.update();
    all.clear();
    for (auto &l : lines) all += l + "\n";
    CHECK(contains(all, "TIMER set 100 VendDone -> Me") && contains(all, "CHANGE Has Credit -> Idle"));
    CHECK(!contains(all, "MSG "));
    // offline filtering of the same format
    TraceFilter f;
    std::map<std::string, bool> ctx;
    CHECK(f.parse("obj=machine,tick=5..9,kind=ACF", &err));
    CHECK(traceLineMatches(f, "[tick 7] obj=machine(0) plan=VENDING beh=Idle ACF AddCredit()", ctx));
    CHECK(!traceLineMatches(f, "[tick 10] obj=machine(0) plan=VENDING beh=Idle ACF AddCredit()", ctx));
    CHECK(!traceLineMatches(f, "[tick 7] obj=machine(0) plan=VENDING beh=Idle MSG Coin data=0 from=1", ctx));
    CHECK(f.parse("msg=Coin", &err));
    CHECK(traceLineMatches(f, "[tick 7] obj=m(0) plan=V beh=Idle MSG Coin data=0 from=1", ctx));
    CHECK(traceLineMatches(f, "[tick 7] obj=m(0) plan=V beh=Idle ACF AddCredit()", ctx));
    CHECK(!traceLineMatches(f, "[tick 8] obj=m(0) plan=V beh=Idle ACF AddCredit()", ctx));
    CHECK(!f.parse("bogus=1", &err));
}

int main() {
    testFloats();
    testVending();
    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::printf("adlib_tools_test: ok\n");
    return failures ? 1 : 0;
}
