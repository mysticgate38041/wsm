#include "../jni/runtime_status.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main() {
    wsm::FeatureFlags flags;wsm::FeatureValue values[wsm::FeatureCount];
    for(size_t i=0;i<wsm::FeatureCount;++i)values[i]={true,wsm::FeatureSpecs[i].initial};
    assert(flags.publish_observation(3,true,values));
    wsm::SessionObservation state;state.build="fixture";state.pid=123;
    state.phase=wsm::SessionPhase::Ready;state.controls=flags.snapshot();state.observed_epoch=3;
    char json[4096];assert(wsm::serialize_status(state,json,sizeof json));
    assert(strstr(json,"\"ready\":true")&&strstr(json,"\"featureRevision\":1"));
    size_t controls=0;for(char *at=json;(at=strstr(at,"\"on\":true"));++at)++controls;
    assert(controls==18&&strstr(json,"\"observed\":null"));
    state.phase=wsm::SessionPhase::Paused;assert(wsm::serialize_status(state,json,sizeof json));
    assert(strstr(json,"\"features\":{}")&&!strstr(json,"\"on\":false"));
    state.phase=wsm::SessionPhase::Fault;state.restore_pending=true;
    assert(wsm::serialize_status(state,json,sizeof json));
    assert(strstr(json,"\"restorePending\":true")&&strstr(json,"\"features\":{}"));
    state.phase=wsm::SessionPhase::Ready;
    assert(wsm::serialize_status(state,json,sizeof json));
    assert(strstr(json,"\"ready\":false")&&strstr(json,"\"features\":{}"));
    state.phase=wsm::SessionPhase::ResetPending;assert(wsm::serialize_status(state,json,sizeof json));
    assert(strstr(json,"\"features\":{}"));
    char small[64];assert(!wsm::serialize_status(state,small,sizeof small));
    assert(strstr(small,"\"ready\":false"));
    assert(!wsm::serialize_status(state,nullptr,0));
    puts("PASS typed status: all18 controls, lifecycle gating, revision, reset, bounded serialization");
}
