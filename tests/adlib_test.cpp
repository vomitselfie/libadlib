// libadlib unit tests (no game data needed): vocabulary parser, runtime-id resolution, by-name
// registration, decision-path codec, PBN writer/reader round trip, and a tiny plan run.
#include <cstdio>

#include "adlib/adlib.h"

using namespace adlib;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("CHECK FAILED %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static const char *kHeader = R"(
//  test header: nothing but enums and comments
#ifndef GUARD
#define GUARD
enum ID_MSG { id_MSG_Ping, /* two */ id_MSG_Pong,
    id_MSG_Initiate, id_MSG_ExitBehavior, id_MSG_NOMSG };
enum ActionFunctionIDs { id_ACF_Count, id_ACF_Reply, id_ACF_NUM };
enum DecisionFunctionIDs { id_DCF_Coin };
enum Behavior_IDs { id_BEH_Plain };
enum AgendaItemIDs { id_AGD_CStuff };          // no CTimerActivity: gets id 1
enum BehaviorSetParamFnIDs { id_SET_PurgeFlag };
enum Sparse { a, b = 10, c, d = 0x20 };        // libadlib extension: explicit values
enum EXT_ActionFunctionIDs { id_ACF_EXT_Fancy };
#endif
)";

static void testVocabulary() {
    Vocabulary v;
    std::string err;
    CHECK(v.loadEnumHeader(kHeader, &err));
    CHECK(err.empty());
    CHECK(v.id(Role::Message, "Pong") == 1);
    CHECK(v.id(Role::Message, "id_MSG_Pong") == 1);
    CHECK(v.id(Role::Message, "Nope") == -1);
    CHECK(std::string(v.name(Role::Action, 1)) == "id_ACF_Reply");
    const Namespace *s = v.find("Sparse");
    CHECK(s && s->id("b") == 10 && s->id("c") == 11 && s->id("d") == 32 && s->size() == 33);
    CHECK(s && s->name(5) == nullptr);
    // Extension namespaces append after the shipped ids.
    CHECK(v.mergeInto(Role::Action, "EXT_ActionFunctionIDs"));
    CHECK(v.id(Role::Action, "EXT_Fancy") == 3);
    CHECK(v.extend(Role::Action, "id_ACF_X_More") == 4);
    CHECK(!v.mergeInto(Role::Action, "EXT_ActionFunctionIDs")); // clash
    Vocabulary bad;
    CHECK(!bad.loadEnumHeader("enum X { a, a };", &err));
    CHECK(!bad.loadEnumHeader("enum X { a, ", &err));
    // Lists.
    v.loadList("Sounds", "22 kHz header\nSnd1 999 0\n# comment\n\n  Snd2 1\n", ListOptions{1});
    CHECK(v.find("Sounds")->id("Snd2") == 1);
}

static void testIds() {
    RuntimeIds def; // no vocabulary: the interpreter's own messages are disabled
    CHECK(def.msgInitiate == -1 && def.msgExitBehavior == -1 && def.msgNone == -1 && def.messageCount == 0);
    auto v = std::make_shared<Vocabulary>();
    v->loadEnumHeader(kHeader);
    std::vector<std::string> missing;
    RuntimeIds r = RuntimeIds::resolve(v.get(), &missing);
    CHECK(r.msgInitiate == 2 && r.msgExitBehavior == 3 && r.msgNone == 4 && r.messageCount == 5);
    CHECK(r.agdTimerActivity == 1 && r.agendaCount == 2);
    CHECK(r.bsetPurgeFlag == 0 && r.bsetAnimation == -1);
    CHECK(r.isetActiveState == -1); // InteractorParam role unbound -> disabled
    CHECK(r.timerSender == -1);      // Object role unbound
    CHECK(!missing.empty());
    Registry reg(v);
    reg.log = nullptr;
    CHECK(reg.action("Reply", [](ActionCtx &) {}) == 1);
    CHECK(reg.hasAction(1));
    CHECK(reg.action("Missing", [](ActionCtx &) {}) == -1);
    CHECK(reg.unresolved().size() == 1);
    CHECK(reg.hasAgenda(1)); // TimerActivity installed at its implicit id
}

static void testPath() {
    uint16_t p = pathRecord(pathRecord(kPathRecord, 3), 5);
    uint16_t net = p & 0x3FFF;
    CHECK(pathReplay(net) == 3);
    CHECK(pathReplay(net) == 5);
}

struct Host : PlanHost {
    int16_t i;
    explicit Host(int16_t x) : i(x) {}
    int16_t objectIndex() const override { return i; }
};

static void testRoundTripAndRun() {
    auto v = std::make_shared<Vocabulary>();
    v->loadEnumHeader(kHeader);
    using namespace build;
    std::vector<std::unique_ptr<Node>> top;
    top.push_back(behavior(0, "A", list(
        onMessage(0, list(changeTo(1)), list(action(0), decide(0, list(action(0), action(1, args(lit(7), str("x"))))))),
        addAgenda(0))));
    top.push_back(behavior(0, "B", list(onMessage(1, {}, list(action(0), changeTo(0)), 0), behaviorParam(0, args(lit(0), lit(1))))));
    top.push_back(endPlan());
    std::vector<uint32_t> w = encodePlan(top);
    CHECK(w.front() == kTokNode && w[1] == DECLARE_BEHAVIOR);
    CHECK(w[w.size() - 2] == kTokNode && w.back() == END_PLAN);
    PlanTree t;
    std::string err;
    CHECK(buildPlanTree(w, LoadHooks{}, t, &err));
    CHECK(encodePlan(t) == w);
    std::vector<uint32_t> back;
    CHECK(readPbnWords(pbnFileBytes(w), back, &err) && back == w);
    CHECK(dumpPlan(t, v.get()).find("DO_ACTION(id_ACF_Reply, 7, \"x\")") != std::string::npos);
    CHECK(!dumpTokens(w).empty());

    // Run it: Ping -> count, coin-flip decision (records the path), go to B.
    Registry reg(v);
    reg.log = nullptr;
    int counted = 0, replied = 0;
    reg.action("Count", [&](ActionCtx &) { ++counted; });
    reg.action("Reply", [&](ActionCtx &c) { ++replied; CHECK(c.arg(1) == 7); });
    reg.decision("Coin", [](ActionCtx &) -> uint16_t { return 1; });
    PlanScene scene;
    Host h(0);
    PlanObject o(scene, h, reg);
    scene.add(&o);
    CHECK(o.loadPlanFromWords("T", w));
    o.start(0, 0);
    scene.update();
    o.sendMessage(v->id(Role::Message, "Ping"), 0, 0);
    scene.update();
    CHECK(counted == 1 && replied == 1);
    CHECK(o.active()->current->name == "B");
    o.sendMessage(v->id(Role::Message, "Pong"), 0, 0);
    scene.update();
    CHECK(counted == 2 && o.active()->current->name == "A");
    // Net replay: the recorded path picks branch 0 without calling the DCF.
    o.receiveMessage(0, 0, uint16_t(0x1000 | 0), 0, true);
    CHECK(counted == 4 && replied == 1);
}

int main() {
    testVocabulary();
    testIds();
    testPath();
    testRoundTripAndRun();
    std::printf("%s (%d failures)\n", fails ? "FAIL" : "adlib_test OK", fails);
    return fails ? 1 : 0;
}
