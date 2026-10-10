#include "dispatcher.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
namespace wsm {
static bool range(float value, float low, float high) {
    return isfinite(value) && value >= low && value <= high;
}
bool toggle_value(const char *text,bool &enabled) {
    float value;char extra;
    if(!text || sscanf(text,"%f %c",&value,&extra)!=1 || !isfinite(value) || (value!=0 && value!=1)) return false;
    enabled=value!=0;return true;
}
bool slider_value(const char *text,float low,float high,float &value) {
    float parsed;char extra;
    if(!text || sscanf(text,"%f %c",&parsed,&extra)!=1 || !isfinite(parsed) || (parsed!=0 && !range(parsed,low,high))) return false;
    value=parsed;return true;
}
bool valid_command(const char *c) {
    if (!c || !*c || *c==' ' || strlen(c)>=192) return false;
    for(const unsigned char *p=reinterpret_cast<const unsigned char *>(c);*p;++p)
        if(*p<32 || *p==127) return false; // exact ASCII spaces; no tab/newline keyword aliases
    if (!strcmp(c,"panic") || !strcmp(c,"sweep") || !strcmp(c,"selftest")) return true;
    char id[48],extra;float v=0,z=0;
    if(!strncmp(c,"feat ",5)) {
        if(sscanf(c+5,"%47s %f %c",id,&v,&extra)!=2 || !isfinite(v) || v<0) return false;
        if(!strcmp(id,"aura")) return v==0 || range(v,5,40);
        if(!strcmp(id,"dmg")) return v==0 || range(v,1,99);
        if(!strcmp(id,"timescale")) return v==0 || range(v,.1f,5);
        const char *fixed[]={"god","hp","stam","mana","poise","immune","ohk","crit","onehp"};
        for(const auto *name:fixed) if(!strcmp(id,name)) return v==0 || v==1;
        return false;
    }
    if(!strncmp(c,"speed ",6)) return slider_value(c+6,1,5,v);
    if(!strncmp(c,"critdmg ",8)) return slider_value(c+8,1,5,v);
    if(!strncmp(c,"tpr ",4)) return sscanf(c+4,"%f %f %c",&v,&z,&extra)==2 && range(v,-100,100) && range(z,-100,100);
    const char *toggle[]={"godmode","nocd","loot","stunall"};bool enabled;
    for(const auto *name:toggle) {const size_t n=strlen(name);
        if(!strncmp(c,name,n) && c[n]==' ') return toggle_value(c+n+1,enabled);
    }
    if(!strncmp(c,"gm ",3)) {
        const char *body=c+3;
        if(!strcmp(body,"reset")||!strcmp(body,"status")||!strcmp(body,"preset list")) return true;
        if(!strncmp(body,"max ",4)) {
            char id[48],extra;
            if(sscanf(body+4,"%47s %c",id,&extra)!=1) return false;
            const char *targets[]={"god","hp","stam","mana","poise","immune","ohk","crit","onehp",
                                   "aura","dmg","timescale","speed","critdmg"};
            for(const auto *name:targets) if(!strcmp(id,name)) return true;
            return false;
        }
        if(!strncmp(body,"all ",4)) {bool on;return toggle_value(body+4,on);}
        if(!strncmp(body,"preset ",7)) {
            const char *p=body+7;
            if(!strncmp(p,"save ",5)||!strncmp(p,"load ",5)) {
                const char *name=p+5;size_t n=0;
                for(;name[n];++n) if(n>=16||!((name[n]>='a'&&name[n]<='z')||(name[n]>='0'&&name[n]<='9')||name[n]=='_')) return false;
                return n>0;
            }
            return false;
        }
        return false;
    }
    return false;
}
void escape_json(const char *s, char *out, size_t cap) {
    if (!out || !cap) return;
    size_t n=0;
    if (s) for (; *s && n+2<cap; ++s) {
        const unsigned char c=static_cast<unsigned char>(*s);
        if (c=='"' || c=='\\') out[n++]='\\';
        out[n++]=c<32 ? ' ' : static_cast<char>(c);
    }
    out[n]=0;
}
}
