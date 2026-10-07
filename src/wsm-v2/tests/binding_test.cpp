#include "../jni/wsm_binding.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

struct Method { const char *name, *returns, *p0, *p1; uint32_t argc; bool stat, generic, inflated; };
static Method rows[] = {
    {"GetBattleFor", "Oak.BattleInstance", "Oak.Party", "System.Boolean", 2, false, false, false},
    {"GetBattleFor", "Oak.BattleInstance", "Oak.IFieldObject", "System.Boolean", 2, false, false, false},
};
static uint32_t length = 2, allocated = 0, released = 0;
static void *methods(void *, void **iterator) {
    uintptr_t i = (uintptr_t)*iterator; *iterator = (void *)(i + 1);
    return i < length ? &rows[i % 2] : nullptr;
}
static const char *name(void *m) { return ((Method *)m)->name; }
static uint32_t argc(void *m) { return ((Method *)m)->argc; }
static void *param(void *m, uint32_t i) { return (void *)(i ? ((Method *)m)->p1 : ((Method *)m)->p0); }
static void *returns(void *m) { return (void *)((Method *)m)->returns; }
static uint32_t flags(void *m, uint32_t *) { return ((Method *)m)->stat ? 0x10 : 0; }
static char *type_name(void *t) { ++allocated; return strdup((char *)t); }
static void release(void *p) { ++released; free(p); }
static bool generic(void *m) { return ((Method *)m)->generic; }
static bool inflated(void *m) { return ((Method *)m)->inflated; }
int main() {
    wsm::BindingApi api{methods,name,argc,param,returns,flags,type_name,release,generic,inflated};
    wsm::Binding wanted{"GetBattleFor", "Oak.BattleInstance", {"Oak.IFieldObject", "System.Boolean", nullptr}, 2, false};
    assert(wsm::resolve_binding(api, rows, wanted).method == &rows[1]);
    rows[0].p0 = "Oak.IFieldObject";
    assert(wsm::resolve_binding(api, rows, wanted).state == wsm::BindingState::Ambiguous);
    rows[0].stat = true;
    assert(wsm::resolve_binding(api, rows, wanted).method == &rows[1]);
    rows[1].returns = "System.Boolean";
    assert(wsm::resolve_binding(api, rows, wanted).state == wsm::BindingState::Missing);
    rows[1].returns = "Oak.BattleInstance"; rows[1].generic = true;
    assert(wsm::resolve_binding(api, rows, wanted).state == wsm::BindingState::Missing);
    rows[1].generic = false; rows[1].inflated = true;
    assert(wsm::resolve_binding(api, rows, wanted).state == wsm::BindingState::Missing);
    rows[1].inflated = false; length = 5000;
    assert(wsm::resolve_binding(api, rows, wanted).state == wsm::BindingState::Truncated);
    assert(allocated == released);
    api.release = nullptr;
    assert(wsm::resolve_binding(api, rows, wanted).state == wsm::BindingState::Unavailable);
    assert(wsm::identity_matches("com.kakaogames.gdts", "3.54.0", 423));
    assert(!wsm::identity_matches("com.kakaogames.gdts", "3.54.0", 424));
    assert(!wsm::identity_matches("com.kakaogames.gdts", "3.54.1", 423));
    assert(!wsm::identity_matches(nullptr, "3.54.0", 423));
    puts("PASS binding: full parameter/return/static contract, ambiguity, generic, enumeration cap, type-name ownership, versionCode identity");
}
