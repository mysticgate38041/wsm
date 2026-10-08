#include "../jni/dispatcher.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main() {
    const char *allowed[]={"panic","sweep","selftest","feat god 1","feat hp 0","feat stam 1","feat mana 1",
    "feat poise 1","feat immune 1","feat timescale 0.25","feat ohk 1","feat dmg 99","feat crit 1","feat aura 5",
    "feat onehp 1","godmode 1","speed 1.25","critdmg 4.75","nocd 1","loot 1","stunall 1","tpr -100 100"};
    for (const char *c:allowed) assert(wsm::valid_command(c));
    const char *rejected[]={"","panic x","feat god nan","feat dmg inf","feat aura 4.99","feat dmg 100","feat timescale -1",
    "feat aggro 1","feat stunall 1","speed 0.5","critdmg 6","feat hp 2","loot 1 trailing","tpr 0 101","mod 5 x","mdmg 1 5"};
    for (const char *c:rejected) assert(!wsm::valid_command(c));
    assert(!wsm::valid_command(nullptr));char out[20];wsm::escape_json("a\"\\\n",out,sizeof out);
    assert(!strcmp(out,"a\\\"\\\\ "));char tiny[2]={'x','x'};wsm::escape_json("quote",tiny,2);assert(tiny[0]==0);
    wsm::escape_json(nullptr,out,sizeof out);assert(out[0]==0);wsm::escape_json("x",nullptr,0);
    const char *noncanonical[]={"godmode\t0","godmode0","speed0","featgod 1"," godmode 0","loot\n1","speed\r0"};
    for(const auto *c:noncanonical) assert(!wsm::valid_command(c));
    float slider=0;assert(wsm::slider_value("-0",1,5,slider)&&slider==0);assert(wsm::slider_value("+0",1,5,slider)&&slider==0);
    assert(wsm::slider_value("01",1,5,slider)&&slider==1);assert(wsm::slider_value("1e0",1,5,slider)&&slider==1);
    assert(!wsm::slider_value("0.5",1,5,slider));assert(!wsm::slider_value("nan",1,5,slider));
    bool enabled=true;
    assert(wsm::toggle_value("-0",enabled)&&!enabled);assert(wsm::toggle_value("+0",enabled)&&!enabled);
    assert(wsm::toggle_value("01",enabled)&&enabled);assert(wsm::toggle_value("1e0",enabled)&&enabled);
    assert(!wsm::toggle_value("2",enabled));assert(!wsm::toggle_value("nan",enabled));assert(!wsm::toggle_value("1 x",enabled));
    puts("dispatcher: command grammar and escaping passed");
}
