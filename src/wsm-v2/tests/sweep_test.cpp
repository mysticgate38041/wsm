#include "../jni/wsm_sweep.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <initializer_list>
using namespace wsm;
enum Method { Stage=1, Manager, Monsters, Active, Stats, Dead, Factory, Mortal,
    Pipeline, Players, Position, HP, Critical, NoCritical };
struct Object { uint64_t header[2]{}; int active=3; bool dead=false; bool garage=false;
    int hp=100; float x=0; } car,monster;
struct Array { uint64_t header[4]{}; void *items[2]{}; } array;
struct List { uint64_t header[2]{}; void *array; int count; } list;
struct Box { uint64_t header[2]{}; alignas(8) uint8_t data[0x300]{}; } box;
struct Damage { Object *object; int value; bool not_mortal,critical,no_critical; };
static void *handles[1024];static uint32_t next_handle;static int live_handles;
static int scene,other_scene,manager,calls,car_calls,monster_calls,mode,invoke_calls;
static int factory_calls,critical_calls,no_critical_calls,mortal_calls;
static int damage_seen[2];static bool critical_seen[2],no_critical_seen[2],mortal_seen[2];
static bool changed_scene,info_created;
static volatile uint64_t allowed=1;
static SweepApi api;
static bool info_pinned(){for(uint32_t h=1;h<=next_handle;++h)if(handles[h]==&box)return true;return false;}
static void *unbox(void *o){return o==&box&&!(mode==15&&info_created)?box.data:nullptr;}
static uint32_t pin(void *o,bool pinned){assert(pinned);if(mode==14&&o==&box)return 0;
    handles[++next_handle]=o;++live_handles;return next_handle;}
static void *target(uint32_t h){return handles[h];}
static void release(uint32_t h){assert(handles[h]);if(handles[h]==&box)info_created=false;
    handles[h]=nullptr;--live_handles;}
static void *invoke(void *method,void *receiver,void **args,void **exception){
    *exception=nullptr;++invoke_calls;
    switch((uintptr_t)method){
    case Stage:return changed_scene||(mode==2&&calls)?&other_scene:&scene;
    case Manager:assert(receiver==&scene);return &manager;
    case Monsters:return &list;
    case Active:memcpy(box.data,&((Object *)receiver)->active,4);return &box;
    case Stats:return receiver;
    case Dead:box.data[0]=((Object *)receiver)->dead;return &box;
    case Factory:{assert(args[0]==&car||args[0]==&monster);assert(!receiver);++factory_calls;
        if(mode==3){*exception=&manager;return nullptr;}
        Damage d{(Object *)args[0],*(int *)args[1],false,false,true};memcpy(box.data,&d,sizeof d);
        info_created=true;
        // Attempt to change the request during execution. This ticket must keep
        // its entry snapshot for the current target and the next target.
        if(mode==13){api.pulse_damage=900000;api.critical=false;api.set_critical=nullptr;
            api.set_no_critical=nullptr;api.one_hp=true;}
        return &box;}
    case Mortal:{assert(receiver==box.data&&info_pinned());++mortal_calls;
        if(mode==4){*exception=&manager;return nullptr;}
        ((Damage *)receiver)->not_mortal=*(bool *)args[0];return nullptr;}
    case Critical:{assert(receiver==box.data&&info_pinned());++critical_calls;
        assert(*(bool *)args[0]);
        if(mode==7){*exception=&manager;return nullptr;}
        ((Damage *)receiver)->critical=true;
        if(mode==9)allowed=0;
        if(mode==10)changed_scene=true;
        return nullptr;}
    case NoCritical:{assert(receiver==box.data&&info_pinned());++no_critical_calls;
        assert(critical_calls==no_critical_calls&&((Damage *)receiver)->critical);
        assert(!*(bool *)args[0]);
        if(mode==8){*exception=&manager;return nullptr;}
        ((Damage *)receiver)->no_critical=false;
        if(mode==11)allowed=0;
        if(mode==12)changed_scene=true;
        return nullptr;}
    case Players:return &list;
    case Position:{float xyz[3]={((Object *)receiver)->x,0,0};memcpy(box.data,xyz,sizeof xyz);return &box;}
    case HP:memcpy(box.data,&((Object *)receiver)->hp,4);return &box;
    case Pipeline:{assert(!receiver&&*(int *)args[0]==0&&info_pinned());
        if(mode==16){*exception=&manager;return nullptr;}
        const Damage *d=(Damage *)args[1];Object *o=d->object;
        assert((o==&car||o==&monster)&&!o->dead&&calls<2);
        damage_seen[calls]=d->value;critical_seen[calls]=d->critical;
        no_critical_seen[calls]=d->no_critical;mortal_seen[calls]=d->not_mortal;
        if(mode==6){assert(d->not_mortal&&!d->critical&&d->no_critical&&d->value==o->hp-1);o->hp=1;}
        else o->dead=true;
        ++calls;o->garage?++car_calls:++monster_calls;
        if(mode==1){array.items[0]=&monster;array.items[1]=nullptr;list.count=1;}
        if(mode==5)allowed=0;
        return nullptr;}
    default:assert(false);return nullptr;
    }
}
static void reset(int m){assert(!live_handles);next_handle=0;memset(handles,0,sizeof handles);
    car={};monster={};car.garage=true;array.items[0]=&car;array.items[1]=&monster;
    array.header[3]=2;list.array=&array;list.count=2;
    calls=car_calls=monster_calls=invoke_calls=factory_calls=critical_calls=no_critical_calls=mortal_calls=0;
    memset(damage_seen,0,sizeof damage_seen);changed_scene=info_created=false;mode=m;allowed=1;
    api={invoke,unbox,pin,target,release,(void *)Stage,(void *)Manager,(void *)Monsters,
        (void *)Active,(void *)Stats,(void *)Dead,(void *)Factory,(void *)Mortal,(void *)Pipeline};
    api.players=(void *)Players;api.position=(void *)Position;api.hp=(void *)HP;
    api.set_critical=(void *)Critical;api.set_no_critical=(void *)NoCritical;
}
static void set_policy(const PulsePolicy &p){api.pulse_damage=p.damage;api.one_hp=p.one_hp;api.critical=p.critical;}
int main(){
    reset(0);auto r=run_sweep(api,&scene,&allowed,1);
    assert(r.state==SweepState::Applied&&r.applied==2&&car_calls==1&&monster_calls==1&&!live_handles);
    assert(damage_seen[0]==1000000&&damage_seen[1]==1000000&&!critical_seen[0]&&no_critical_seen[0]);
    reset(1);r=run_sweep(api,&scene,&allowed,1);assert(r.applied==2&&calls==2&&!live_handles);
    reset(2);r=run_sweep(api,&scene,&allowed,1);assert(r.state==SweepState::Stale&&calls==1&&!live_handles);
    reset(3);r=run_sweep(api,&scene,&allowed,1);assert(r.state==SweepState::Failed&&!calls&&!live_handles);
    reset(4);r=run_sweep(api,&scene,&allowed,1);assert(r.state==SweepState::Failed&&!calls&&!live_handles);
    reset(5);r=run_sweep(api,&scene,&allowed,1);assert(r.state==SweepState::Cancelled&&calls==1&&!live_handles);
    reset(0);allowed=0;r=run_sweep(api,&scene,&allowed,1);assert(r.state==SweepState::Cancelled&&!invoke_calls&&!live_handles);
    reset(0);car.dead=true;r=run_sweep(api,&scene,&allowed,1);assert(r.applied==1&&r.skipped==1&&!car_calls&&!live_handles);
    reset(0);r=run_sweep(api,&other_scene,&allowed,1);assert(r.state==SweepState::Stale&&!calls&&!live_handles);
    reset(0);array.header[3]=1;r=run_sweep(api,&scene,&allowed,1);assert(r.state==SweepState::Failed&&!calls&&!live_handles);
    reset(0);api.radius=20;monster.x=21;r=run_sweep(api,&scene,&allowed,1);assert(r.applied==1&&r.skipped==1&&!monster_calls&&!live_handles);
    reset(6);api.radius=20;api.one_hp=true;api.critical=true;api.pulse_damage=9900000;
    api.set_critical=api.set_no_critical=nullptr;
    r=run_sweep(api,&scene,&allowed,1);assert(r.applied==2&&!car.dead&&!monster.dead&&!critical_calls&&!live_handles);
    r=run_sweep(api,&scene,&allowed,1);assert(!r.applied&&r.skipped==2&&calls==2&&!live_handles);
    reset(0);api.probe_only=true;r=run_sweep(api,&scene,&allowed,1);assert(r.state==SweepState::Applied&&!factory_calls&&!calls&&!live_handles);
    PulsePolicy p{};int checked=123;
    for(float value:{1.0f,10.0f,99.0f}){
        assert(pulse_power_damage(value,checked)&&checked==(int)value*100000);
        assert(pulse_policy(true,false,false,true,value,false,p));
        reset(0);set_policy(p);api.radius=20;r=run_sweep(api,&scene,&allowed,1);
        assert(r.applied==2&&damage_seen[0]==checked&&damage_seen[1]==checked&&!critical_calls&&!live_handles);
    }
    for(float invalid:{0.0f,-1.0f,100.0f,1.5f,(float)INFINITY,(float)NAN}){
        checked=123;assert(!pulse_power_damage(invalid,checked)&&checked==123);
        p.damage=700000;assert(!pulse_policy(true,false,false,true,invalid,false,p)&&p.damage==700000);
    }
    assert(pulse_policy(true,false,false,false,10,true,p)&&p.damage==1000000&&p.critical&&!p.one_hp);
    reset(0);set_policy(p);r=run_sweep(api,&scene,&allowed,1);
    assert(r.applied==2&&critical_calls==2&&no_critical_calls==2&&critical_seen[0]&&!no_critical_seen[0]&&!live_handles);
    assert(pulse_policy(true,false,true,true,99,true,p)&&p.one_hp&&!p.critical&&p.damage==1000000);
    assert(pulse_policy(true,true,true,true,99,true,p)&&!p.one_hp&&p.critical&&p.damage==9900000);
    reset(0);set_policy(p);r=run_sweep(api,&scene,&allowed,1);
    assert(r.applied==2&&damage_seen[0]==9900000&&!mortal_seen[0]&&critical_seen[0]&&!live_handles);
    assert(pulse_policy(false,true,true,true,99,true,p)&&p.damage==1000000&&!p.one_hp&&!p.critical);
    reset(0);set_policy(p);r=run_sweep(api,&scene,&allowed,1);
    assert(r.applied==2&&damage_seen[0]==1000000&&!critical_calls&&!mortal_seen[0]&&!live_handles);
    for(int missing=0;missing<2;++missing){reset(0);api.critical=true;
        if(missing)api.set_critical=nullptr;else api.set_no_critical=nullptr;
        r=run_sweep(api,&scene,&allowed,1);assert(r.state==SweepState::Failed&&!invoke_calls&&!live_handles);}
    for(int error:{7,8,14,15,16}){reset(error);api.critical=true;
        r=run_sweep(api,&scene,&allowed,1);assert(r.state==SweepState::Failed&&!calls&&!live_handles);
        if(error==7)assert(critical_calls==1&&!no_critical_calls);}
    for(int cancel:{9,11}){reset(cancel);api.critical=true;r=run_sweep(api,&scene,&allowed,1);
        assert(r.state==SweepState::Cancelled&&!calls&&!live_handles);}
    for(int stale:{10,12}){reset(stale);api.critical=true;r=run_sweep(api,&scene,&allowed,1);
        assert(r.state==SweepState::Stale&&!calls&&!live_handles);}
    reset(13);api.critical=true;api.pulse_damage=9900000;r=run_sweep(api,&scene,&allowed,1);
    assert(r.applied==2&&damage_seen[0]==9900000&&damage_seen[1]==9900000&&critical_calls==2&&no_critical_calls==2&&!mortal_seen[1]&&!live_handles);
    assert(api.pulse_damage==900000&&api.one_hp&&!api.critical);
    reset(0);api.pulse_damage=0;r=run_sweep(api,&scene,&allowed,1);assert(r.state==SweepState::Failed&&!invoke_calls&&!live_handles);
    printf("sweep PASS: power bounds, critical/noCritical, immutable policy, ONEHP/OHK/clear, errors/cancel/scene/pins; SweepApi=%zu\n",sizeof(SweepApi));
}
