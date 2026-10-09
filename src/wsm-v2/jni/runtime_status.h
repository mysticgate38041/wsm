#pragma once
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "feature_flags.h"

namespace wsm {
enum class SessionPhase { Booting, IdentityPending, Paused, ScenePending, Ready, ResetPending, Fault };
inline const char *phase_name(SessionPhase phase) {
    switch (phase) {
        case SessionPhase::Booting:return "booting";
        case SessionPhase::IdentityPending:return "identity_pending";
        case SessionPhase::Paused:return "paused";
        case SessionPhase::ScenePending:return "scene_pending";
        case SessionPhase::Ready:return "ready";
        case SessionPhase::ResetPending:return "reset_pending";
        case SessionPhase::Fault:return "fault";
    }
    return "fault";
}
struct SessionObservation {
    SessionPhase phase=SessionPhase::Booting;
    const char *build=""; // Compiled build identifier, never external input.
    int pid=0, faults=0, players=0, loot_candidates=0, loot_requested=0;
    bool payload=false, restore_pending=false, time_modifier_owned=false;
    unsigned owned_objects=0, owned_bits=0, uncertain_objects=0;
    uint64_t observed_epoch=0;
    FeatureSnapshot controls{};
};
class StatusWriter {
    char *out_;size_t capacity_,used_=0;bool valid_;
public:
    StatusWriter(char *out,size_t capacity):out_(out),capacity_(capacity),valid_(out&&capacity) {
        if(valid_)out_[0]=0;
    }
    void append(const char *format,...) {
        if(!valid_)return;
        va_list args;va_start(args,format);
        const int n=vsnprintf(out_+used_,capacity_-used_,format,args);
        va_end(args);
        if(n<0||static_cast<size_t>(n)>=capacity_-used_) {valid_=false;return;}
        used_+=static_cast<size_t>(n);
    }
    bool valid() const { return valid_; }
};
inline bool serialize_status(const SessionObservation &s,char *out,size_t capacity) {
    StatusWriter w(out,capacity);
    const bool ready=s.phase==SessionPhase::Ready&&s.controls.ready&&!s.restore_pending;
    w.append("{\"build\":\"%s\",\"state\":\"%s\",\"ready\":%s,\"pid\":%d,\"epoch\":%llu,\"observedEpoch\":%llu,"
             "\"featureRevision\":%llu,\"faults\":%d,\"payload\":%s,\"players\":%d,\"lootCandidates\":%d,\"lootRequested\":%d,"
             "\"restorePending\":%s,\"ownedOptionObjects\":%u,\"ownedOptionBits\":%u,\"uncertainOptionObjects\":%u,"
             "\"timeModifierOwned\":%s,\"observed\":null,\"features\":{",
             s.build,phase_name(s.phase),ready?"true":"false",s.pid,(unsigned long long)s.controls.epoch,
             (unsigned long long)s.observed_epoch,(unsigned long long)s.controls.revision,s.faults,s.payload?"true":"false",
             s.players,s.loot_candidates,s.loot_requested,s.restore_pending?"true":"false",s.owned_objects,s.owned_bits,
             s.uncertain_objects,s.time_modifier_owned?"true":"false");
    // An unavailable/restoring session cannot confirm OFF. Omit uncertain controls.
    if(ready) {
        for(size_t i=0;i<FeatureCount;++i)
            w.append("%s\"%s\":{\"on\":%s,\"value\":%.3f}",i?",":"",FeatureSpecs[i].id,
                     s.controls.features[i].enabled?"true":"false",(double)s.controls.features[i].value);
    }
    w.append("}}");
    if(!w.valid()&&out&&capacity)
        snprintf(out,capacity,"{\"state\":\"fault\",\"ready\":false,\"features\":{}}");
    return w.valid();
}
}
