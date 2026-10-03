// ADLIB vocabulary: the symbol table that gives the numbers in a compiled plan (.PBN) their
// meaning. The original ADLIB compiler built it at run time from text files (enumIDs.h: "nothing
// but enum statements and comments", plus the animation-assembly and sound lists); the VM's
// primitive tables were then filled by game code at start-up. libadlib keeps that split: the
// vocabulary is data, and hosts register primitives *by name* against it.
//
// A vocabulary is a set of namespaces (one per enum / list file). Each namespace maps
// name <-> id. The interpreter's own needs (messages, actions, decisions, ...) are *roles* bound
// to namespaces; by default each role is bound to the enum name the original enumIDs.h used
// (ID_MSG, ActionFunctionIDs, ...), so a new game can simply reuse those enum names.
// See docs/host-api.md.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace adlib {

// The namespaces the interpreter itself consumes.
enum class Role : uint8_t {
    Message,          // enum ID_MSG                 (message interactors, SET_TIMEOUTMSG)
    CollisionObject,  // enum CollisionObjectID      (collision interactors)
    Behavior,         // enum Behavior_IDs           (DECLARE_BEHAVIOR type -> behaviour factory)
    Agenda,           // enum AgendaItemIDs          (ADD/REMOVE_AGENDA_ITEM)
    Action,           // enum ActionFunctionIDs      (DO_ACTION)
    Decision,         // enum DecisionFunctionIDs    (DECIDE_BY_*)
    BehaviorParam,    // enum BehaviorSetParamFnIDs  (SET_BEHAVIOR_PARAM)
    InteractorParam,  // enum InteractorSetParamFnIDs (SET_INTERACTOR_PARAM)
    Pronoun,          // enum Pronouns               (0xFEDCxxxx leaves)
    Object,           // enum ID_MOD                 (game object ids; optional)
    Count
};
const char *roleName(Role r);              // "Message", "Action", ...
const char *defaultEnumForRole(Role r);    // "ID_MSG", "ActionFunctionIDs", ...

class Namespace {
public:
    explicit Namespace(std::string name = {}) : name_(std::move(name)) {}
    const std::string &name() const { return name_; }

    // Adds `sym` with `id` (or the next free id when id < 0). Returns the id, or -1 when the
    // name already exists with a different id.
    int add(const std::string &sym, int id = -1);
    int append(const std::string &sym) { return add(sym, -1); }

    // Full name ("id_ACF_SendMessage") or short name ("SendMessage": the leading id_XXX_ prefix
    // stripped) -> id; -1 when unknown.
    int id(std::string_view sym) const;
    const char *name(int id) const;      // full name or nullptr
    std::string shortName(int id) const; // without the id_XXX_ prefix
    // Number of ids (max id + 1). For enums this counts the trailing sentinel (id_MSG_NOMSG ...).
    int size() const { return int(names_.size()); }
    const std::vector<std::string> &names() const { return names_; } // index = id ("" = gap)

    static std::string stripPrefix(std::string_view full); // "id_ACF_SendMessage" -> "SendMessage"

private:
    std::string name_;
    std::vector<std::string> names_;
    std::map<std::string, int, std::less<>> byName_;
    std::map<std::string, int, std::less<>> byShort_;
};

struct ListOptions {
    size_t skipLines = 0;          // header lines to ignore (e.g. SNDFILES.LST's first line)
    std::string comment = "#";     // lines whose first non-blank characters are this are skipped
    std::string requirePrefix;     // only names starting with this count (e.g. "AA_")
};

class Vocabulary {
public:
    Vocabulary();

    // ---- namespaces --------------------------------------------------------------------------
    Namespace &ns(const std::string &name);              // get or create
    const Namespace *find(const std::string &name) const;
    std::vector<std::string> namespaceNames() const;     // in creation order

    // ---- loaders -------------------------------------------------------------------------------
    // enumIDs.h-style text: `enum Name { a, b, c };` blocks, // and /* */ comments; anything
    // outside braces (#ifndef guards ...) is ignored, like the original tokenizer did. Values are
    // the 0-based position in the enum (the original's only rule); `name = value` is accepted as
    // a libadlib extension (later names continue from value + 1). Returns false and sets `err`
    // on malformed text. An enum whose name already exists is appended to (ids continue).
    bool loadEnumHeader(std::string_view text, std::string *err = nullptr);
    bool loadEnumHeaderFile(const std::string &path, std::string *err = nullptr);
    // One symbol per line (the first whitespace-separated token); id = order of appearance.
    // Used for AnimAssm.txt-, Sndfiles.lst- and Infobtxt.lst-style lists.
    void loadList(const std::string &nsName, std::string_view text, const ListOptions &opt = {});
    bool loadListFile(const std::string &nsName, const std::string &path, const ListOptions &opt = {},
                      std::string *err = nullptr);

    // ---- roles ---------------------------------------------------------------------------------
    void bind(Role r, const std::string &nsName); // default: defaultEnumForRole(r)
    const std::string &binding(Role r) const { return bind_[size_t(r)]; }
    const Namespace *role(Role r) const { return find(binding(r)); }
    Namespace &roleNs(Role r) { return ns(binding(r)); }

    int id(Role r, std::string_view sym) const;  // -1 if unknown / role unbound
    const char *name(Role r, int id) const;      // nullptr if unknown

    // Extension primitives (docs/host-api.md "Extending a shipped vocabulary"): appends
    // `sym` to the role's namespace after every existing id, so shipped PBN ids never move.
    // Use a distinctive prefix (id_ACF_HBX_..., id_ACF_EXT_...). Returns the new id.
    int extend(Role r, const std::string &sym);

    // Appends all symbols of namespace `from` (e.g. an EXT_ActionFunctionIDs enum loaded from a
    // second header) to the namespace bound to `r`. Returns false on a name clash.
    bool mergeInto(Role r, const std::string &from);

private:
    std::vector<std::unique_ptr<Namespace>> order_;
    std::map<std::string, Namespace *> byName_;
    std::vector<std::string> bind_;
};

} // namespace adlib
