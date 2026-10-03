// libadlib example: the vending machine (the explanatory example of Rosen & Duisberg's 1996
// ADLIB article), on a console. A complete host in one file:
//
//   1. the vocabulary comes from this game's own VENDING.H (enums) and VENDTEXT.LST (a list);
//   2. the primitives (actions, decisions, pronouns) are registered by name against it;
//   3. the plans are ADLIB source (VENDING.txt, CUSTOMER.txt) that adlibc compiled to .PBN files
//      at build time against the same VENDING.H and VENDTEXT.LST; they are loaded like a shipped
//      game loads its plans;
//   4. a scripted customer drives the machine through PlanScene::update().
//
//   ./vending_machine [data dir (VENDING.H, VENDTEXT.LST)] [dir of the compiled .PBN files] [-v] [--trace FILTER]
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "adlib/adlib.h"
#include "adlib/tools.h"

using namespace adlib;

#ifndef VENDING_DIR
#define VENDING_DIR "."
#endif
#ifndef VENDING_PBN_DIR
#define VENDING_PBN_DIR "." // where CMake's adlibc step wrote VENDING.PBN and CUSTOMER.PBN
#endif

namespace {

// The host objects. PlanHost is all the runtime needs to know about them.
struct Thing : PlanHost {
    int16_t idx;
    std::string name;
    Thing(int16_t i, std::string n) : idx(i), name(std::move(n)) {}
    int16_t objectIndex() const override { return idx; }
};
struct Machine : Thing {
    int credit = 0, stock = 1, price = 2;
    using Thing::Thing;
};

std::map<int, std::string> texts; // VENDTEXT.LST id -> text

std::string readText(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

int main(int argc, char **argv) {
    std::string dataDir = VENDING_DIR, pbnDir = VENDING_PBN_DIR;
    bool verbose = false;
    std::string traceFilter; // --trace FILTER: adlib::tools::Tracer (docs/tools.md)
    int frame = 0;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-v") verbose = true;
        else if (std::string(argv[i]) == "--trace" && i + 1 < argc) traceFilter = argv[++i];
        else pos.push_back(argv[i]);
    }
    if (pos.size() > 0) dataDir = pos[0];
    if (pos.size() > 1) pbnDir = pos[1];

    // ---- 1. vocabulary ------------------------------------------------------------------------
    auto vocab = std::make_shared<Vocabulary>();
    std::string err;
    if (!vocab->loadEnumHeaderFile(dataDir + "/VENDING.H", &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::string listText = readText(dataDir + "/VENDTEXT.LST");
    vocab->loadList("Texts", listText);
    {   // the host keeps the texts themselves; the vocabulary gives the ids
        std::istringstream ls(listText);
        std::string line;
        while (std::getline(ls, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t sp = line.find_first_of(" \t");
            std::string name = line.substr(0, sp);
            size_t b = line.find_first_not_of(" \t", sp);
            texts[vocab->find("Texts")->id(name)] = b == std::string::npos ? "" : line.substr(b);
        }
    }
    const Vocabulary &V = *vocab;
    auto msg = [&](const char *n) { return V.id(Role::Message, n); };

    // ---- 2. primitives, by name ---------------------------------------------------------------
    Registry reg(vocab);
    reg.log = [](const std::string &s) { std::printf("  [adlib] %s\n", s.c_str()); };
    if (verbose) reg.trace = [](PlanObject &o, const std::string &s) { std::printf("      [%d] %s\n", o.index(), s.c_str()); };
    PlanScene scene;
    Machine machine(0, "machine");
    Thing customer(1, "customer");
    if (!traceFilter.empty()) {
        auto tracer = tools::Tracer::attach(reg, traceFilter, &err);
        if (!tracer) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        tracer->sink = [](const std::string &l) { std::printf("      %s\n", l.c_str()); };
        tracer->tick = [&frame](PlanObject &) { return int64_t(frame); };
        tracer->objectName = [&](PlanObject &o) { return o.index() == 0 ? machine.name : customer.name; };
    }
    auto M = [&](ActionCtx &) -> Machine & { return machine; };

    reg.action("AddCredit", [&](ActionCtx &c) { M(c).credit += 1; });
    reg.action("Display", [&](ActionCtx &c) {
        int t = int(c.arg(1));
        std::printf("  machine display: %s", texts[t].c_str());
        if (t == V.find("Texts")->id("T_Credit")) std::printf(" %d", M(c).credit);
        std::printf("\n");
    });
    reg.action("Dispense", [&](ActionCtx &c) {
        M(c).stock -= 1;
        M(c).credit -= M(c).price;
        std::printf("  *clunk* a snack drops (stock now %d)\n", M(c).stock);
    });
    reg.action("ReturnChange", [&](ActionCtx &c) {
        if (M(c).credit > 0) std::printf("  machine returns %d coin(s)\n", M(c).credit);
        M(c).credit = 0;
    });
    // SendMessage(recipient, msg): queued through the scene.
    reg.action("SendMessage", [](ActionCtx &c) {
        PlanObject *to = c.obj.scene().object(int16_t(c.argResolved(1)));
        if (to) to->sendMessage(int(c.arg(2)), c.msgData, c.obj.index());
    });
    reg.action("Say", [&](ActionCtx &c) { std::printf("  customer: %s\n", texts[int(c.arg(1))].c_str()); });
    // Decisions return the branch index (DECIDE_BY_AMONG children after the function id).
    reg.decision("EnoughCredit", [&](ActionCtx &c) -> uint16_t { return M(c).credit >= M(c).price ? 1 : 0; });
    reg.decision("InStock", [&](ActionCtx &c) -> uint16_t { return M(c).stock > 0 ? 1 : 0; });
    reg.behavior("Machine", [] { return std::make_unique<Behavior>(); });
    reg.behavior("Person", [] { return std::make_unique<Behavior>(); });
    // Pronouns: "Me" is fixed when the plan loads, "Customer" is looked up whenever it is used.
    const int prnMe = V.id(Role::Pronoun, "Me");
    reg.loadPronoun = [prnMe](PlanObject &o, uint16_t p, uint32_t tok) { return p == prnMe ? uint32_t(o.index()) : tok; };
    reg.runtimePronoun("Customer", [&](PlanObject &) { return int32_t(customer.idx); });
    if (!reg.unresolved().empty()) return 1;

    // ---- 3. plans: the .PBN files adlibc compiled from VENDING.txt / CUSTOMER.txt -------------
    PlanObject mObj(scene, machine, reg), cObj(scene, customer, reg);
    scene.add(&mObj);
    scene.add(&cObj);
    if (!mObj.loadPlansFromDir(pbnDir, {"VENDING"}) || !cObj.loadPlansFromDir(pbnDir, {"CUSTOMER"})) {
        std::fprintf(stderr, "cannot load VENDING.PBN / CUSTOMER.PBN from %s (build the vending_plans target)\n",
                     pbnDir.c_str());
        return 1;
    }
    std::printf("\n%s\n", dumpPlan(*mObj.plan(0).tree, vocab.get()).c_str());

    // ---- 4. run -------------------------------------------------------------------------------
    mObj.start(0, 0);
    cObj.start(0, 0);
    struct Step { int frame; const char *event; };
    const Step script[] = {{2, "Select"}, {4, "Coin"}, {6, "Select"}, {8, "Coin"}, {10, "Select"},
                           {12, "Coin"}, {30, "Coin"}, {31, "Coin"}, {32, "Coin"}, {33, "Select"}, {35, "Refund"}};
    size_t next = 0;
    for (frame = 0; frame < 40; ++frame) {
        scene.clock += 100; // ms
        while (next < sizeof script / sizeof *script && script[next].frame == frame) {
            std::printf("t=%4ums customer: %s\n", scene.clock, script[next].event);
            mObj.sendMessage(msg(script[next].event), 0, customer.idx);
            ++next;
        }
        std::string before = mObj.active()->current->name;
        scene.update();
        std::string after = mObj.active()->current->name;
        if (after != before) std::printf("t=%4ums machine: %s -> %s\n", scene.clock, before.c_str(), after.c_str());
    }
    std::printf("done: credit %d, stock %d\n", machine.credit, machine.stock);
    return 0;
}
