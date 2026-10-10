#include "../jni/dispatcher.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main() {
    const char *allowed[]={"panic","sweep","selftest","feat god 1","feat hp 0","feat stam 1","feat mana 1",
    "feat poise 1","feat immune 1","feat timescale 0.25","feat ohk 1","feat dmg 99","feat crit 1","feat aura 5",
    "feat onehp 1","godmode 1","speed 1.25","critdmg 4.75","nocd 1","loot 1","stunall 1","tpr -100 100",
    "gm reset","gm status","gm preset list","gm max god","gm max hp","gm max stam","gm max mana","gm max poise",
    "gm max immune","gm max ohk","gm max crit","gm max onehp","gm max aura","gm max dmg","gm max timescale",
    "gm max speed","gm max critdmg","gm all 0","gm all 1","gm preset save solo","gm preset save farm_1",
    "gm preset load solo","gm preset load a1b2c3","gm preset save 1234567890123456",
    "feat aggro 0","feat aggro 1","gm pos list","gm pos save 0","gm pos save 7","gm pos load 3",
    "fov 0","fov 2","fov 40"};
    for (const char *c:allowed) assert(wsm::valid_command(c));
    const char *rejected[]={"","panic x","feat god nan","feat dmg inf","feat aura 4.99","feat dmg 100","feat timescale -1",
    "feat stunall 1","speed 0.5","critdmg 6","feat hp 2","loot 1 trailing","tpr 0 101","mod 5 x","mdmg 1 5",
    "gm","gm ","gm reset x","gm status x","gm max","gm max foo","gm max dmg 99","gm max god 1","gm all","gm all 2",
    "gm all on","gm preset","gm preset save","gm preset load","gm preset save bad-name","gm preset save TOOLONGNAME",
    "gm preset save 12345678901234567","gm preset load x y","gm preset list x","gm x",
    "gm pos","gm pos save","gm pos save 8","gm pos save x","gm pos load -1","gm pos rm 1","gm pos list x","gm pos save 3 y",
    "fov 1.99","fov 40.01","fov x","fov"};
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
