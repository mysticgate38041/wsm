// Production entry points. Legacy experimental commands are not exposed by this dispatcher.
static void json_quote(const char *s,char *out,size_t cap){wsm::escape_json(s,out,cap);}
static bool modern_valid(const char *c){return wsm::valid_command(c);}
static void modern_request(const char *c, char *out, size_t n, uint64_t expected_epoch) {
    if (strncmp(c, "epoch ", 6) == 0) {
        unsigned long long epoch = 0; int body = 0;
        if (expected_epoch || sscanf(c + 6, "%llu %n", &epoch, &body) != 1 || !epoch || !body || !c[6 + body]) {
            snprintf(out, n, "{\"state\":\"rejected\",\"detail\":\"invalid expected epoch\"}"); return;
        }
        modern_request(c + 6 + body, out, n, epoch); return;
    }
    if (strcmp(c, "status") == 0) { g_runtime.snapshot(out, n); return; }
    if (strcmp(c, "telemetry") == 0) { g_runtime.telemetry(out, n); return; }
    int page = 0; char catalog_tail;
    if (strcmp(c, "catalog") == 0 || sscanf(c, "catalog %d %c", &page, &catalog_tail) == 1) {
        if (page < 0 || page >= 12) snprintf(out, n, "{\"state\":\"rejected\",\"detail\":\"catalog page must be 0..11\"}");
        else snprintf(out, n, "%s", WSM_CATALOG_PAGES[page]);
        return;
    }
    unsigned long long id = 0; char tail;
    if (sscanf(c, "result %llu %c", &id, &tail) == 1) {
        wsm::Result r{};
        if (!g_runtime.result(id, r)) { snprintf(out, n, "{\"state\":\"expired\"}"); return; }
        char escaped[1024]; json_quote(r.detail, escaped, sizeof escaped);
        snprintf(out, n, "{\"id\":%llu,\"epoch\":%llu,\"state\":\"%s\",\"detail\":\"%s\"}",
                 id, (unsigned long long)r.epoch, wsm::name(r.state), escaped);
        return;
    }
    if (!modern_valid(c)) { snprintf(out, n, "{\"state\":\"rejected\",\"detail\":\"unsupported command or invalid range\"}"); return; }
    uint64_t seq = 0;
    if (!g_runtime.submit(c, seq, expected_epoch)) { snprintf(out, n, "{\"state\":\"rejected\",\"detail\":\"epoch changed, queue full or command too long\"}"); return; }
    snprintf(out, n, "{\"id\":%llu,\"state\":\"accepted\"}", (unsigned long long)seq);
}
// Adapter: only this owner translates legacy backend state into public observations.
// Queue/results and serializers depend on no game pointer or managed API.
static void modern_publish() {
    const uint64_t epoch=g_runtime.epoch();
    wsm::SessionObservation observation{};
    observation.build=WSM_BUILD_STAMP;
    observation.pid=getpid();observation.observed_epoch=g_control_epoch;
    const bool current=g_control_epoch==epoch;
    const bool identity=__atomic_load_n(&g_identity_ok,__ATOMIC_ACQUIRE);
    const bool foreground=__atomic_load_n(&g_foreground,__ATOMIC_ACQUIRE);
    observation.phase=!current?wsm::SessionPhase::ResetPending:
        g_session_fault?wsm::SessionPhase::Fault:!identity?wsm::SessionPhase::IdentityPending:
        !foreground?wsm::SessionPhase::Paused:!g_scene_hero?wsm::SessionPhase::ScenePending:wsm::SessionPhase::Ready;
    wsm::FeatureValue values[wsm::FeatureCount]{};
    for(size_t i=0;i<wsm::FeatureCount;++i) values[i]={false,wsm::FeatureSpecs[i].initial};
    for(int i=0;i<FEAT_COUNT;++i) {
        const int index=wsm::feature_index(g_feats[i].id);
        if(index>=0) values[index]={g_feats[i].on,g_feats[i].value};
    }
    values[(size_t)wsm::FeatureIndex::Godmode]={g_godmode_on,1};
    values[(size_t)wsm::FeatureIndex::Speed]={g_speed_on!=0,g_speed_value};
    values[(size_t)wsm::FeatureIndex::NoCooldown]={g_nocd_on!=0,1};
    values[(size_t)wsm::FeatureIndex::Loot]={g_loot_on!=0,1};
    values[(size_t)wsm::FeatureIndex::StunAll]={g_stunall_on!=0,1};
    values[(size_t)wsm::FeatureIndex::CriticalDamage]={g_critdmg_on,g_critdmg_value};
    if(!g_feature_flags.publish_observation(epoch,observation.phase==wsm::SessionPhase::Ready,values)) {
        g_feature_flags.reset(epoch);observation.phase=wsm::SessionPhase::Fault;
    }
    observation.controls=g_feature_flags.snapshot();
    observation.faults=(int)g_guard_faults;
    observation.payload=g_h64_handle&&!g_bus_unhealthy;
    observation.players=g_last_char_count;
    observation.loot_candidates=g_loot_last_n;observation.loot_requested=g_loot_last_done;
    observation.restore_pending=g_restoration_pending;
    observation.owned_objects=wsm::owned_option_objects(g_owned_options,64);
    observation.owned_bits=wsm::owned_option_bits(g_owned_options,64);
    observation.uncertain_objects=wsm::uncertain_option_objects(g_owned_options,64);
    observation.time_modifier_owned=g_timescale_owned;
    char out[4096];
    wsm::serialize_status(observation,out,sizeof out);
    g_runtime.publish_for_epoch(out,epoch);
}
static void modern_reset(char *out, size_t cap) {
    const uint64_t reset_epoch=g_runtime.epoch();
    ctl_exec("panic", out, cap);
    if (strncmp(out,"OK ",3) != 0 || g_bus_unhealthy) g_session_fault = true;
    g_godmode_on = false;
    g_scratched_n = 0; g_scratch_reset = 1;
    g_control_epoch=reset_epoch;
    g_feature_flags.reset(reset_epoch);
}
static void modern_commands() {
    wsm::Command c{};
    // Limit command work per beat so continuous producers cannot starve updates.
    for (int budget = 0; budget < 4 && g_runtime.pop(c); ++budget) {
        char ack[512] = "ERR execution fault";
        bool ok = false;
        if(main_sweep_pending()) {
            if(strcmp(c.text,"panic")==0 && !g_main_reset_command.id){main_sweep_cancel();g_main_reset_command=c;}
            else g_runtime.complete(c,wsm::Outcome::Rejected,"UnityMain sweep in progress; retry after completion");
            continue;
        }
        if (strcmp(c.text, "panic") != 0 && c.epoch != g_runtime.epoch()) {
            g_runtime.complete(c, wsm::Outcome::Stale, "scene/activity changed; submit again"); continue;
        }
        if (strcmp(c.text, "panic") != 0 &&
            (!__atomic_load_n(&g_identity_ok, __ATOMIC_ACQUIRE) ||
             !__atomic_load_n(&g_foreground, __ATOMIC_ACQUIRE) || !g_scene_hero || g_session_fault)) {
            g_runtime.complete(c, wsm::Outcome::Rejected, "session not ready (identity, scene, foreground or fault)"); continue;
        }
        const sig_atomic_t faults = g_guard_faults;
        bool deferred=false;
        BEAT_BEGIN();
        if (strcmp(c.text, "panic") == 0) {
            modern_reset(ack, sizeof ack); ok = !g_bus_unhealthy && strncmp(ack, "OK ", 3) == 0;
        } else if(strncmp(c.text,"feat dmg ",9)==0 || strncmp(c.text,"feat crit ",10)==0) {
            const bool damage=strncmp(c.text,"feat dmg ",9)==0;
            ok=main_pulse_configure(damage,strtof(c.text+(damage?9:10),nullptr),ack,sizeof ack);
        } else if(strcmp(c.text,"sweep")==0) {
            deferred=main_sweep_start(c,ack,sizeof ack);ok=deferred;
        } else if (strcmp(c.text, "selftest") == 0) {
            if (hook_ensure_payload()) {
                uint64_t tok = bus_arm();
                if (tok) {
                    g_h64_bus[4] = 6; g_h64_bus[5] = 7;
                    wsm::send_bus_command(g_h64_bus,2ULL);
                    ok = bus_done(tok) && g_h64_bus[6] == 42;
                }
            }
            if(ok) {
                uint64_t tok=bus_arm();ok=false;
                if(tok){wsm::send_bus_command(g_h64_bus,16ULL);ok=bus_done(tok)&&g_h64_bus[6]==42&&(int64_t)g_h64_bus[68]==0;}
            }
            snprintf(ack, sizeof ack, "%s payload multiply=42 branch/hero-getter/god/neighbor/restore", ok ? "OK" : "ERR");
            if(ok){deferred=main_sweep_start(c,ack,sizeof ack);ok=deferred;}
        } else if (strncmp(c.text, "speed ", 6) == 0) {
            float v = strtof(c.text + 6, nullptr); feat_speed(ack, sizeof ack, c.text + 6);
            ok = v == 0 ? !g_speed_on && !g_bus_unhealthy && strncmp(ack, "OK ", 3) == 0 : g_speed_on;
        } else if (strncmp(c.text, "critdmg ", 8) == 0) {
            float v = strtof(c.text + 8, nullptr); feat_critdmg(ack, sizeof ack, v);
            ok = strncmp(ack, "OK ", 3) == 0 && !g_bus_unhealthy && (v == 0 ? !g_critdmg_on : g_critdmg_on);
        } else if (strncmp(c.text, "godmode ", 8) == 0) {
            bool enabled=false;wsm::toggle_value(c.text+8,enabled);
            if (!enabled) { feat_godoff(ack, sizeof ack); ok = !g_godmode_on && !g_bus_unhealthy && strncmp(ack, "OK ", 3) == 0; }
            else if(g_godmode_on){ok=true;snprintf(ack,sizeof ack,"OK GOD already ON (owned slot unchanged)");}
            else { feat_godmode(ack, sizeof ack, ""); ok = strncmp(ack, "GOD armed ", 10) == 0 && g_h64_bus[69] > 0 && (int64_t)g_h64_bus[68] == 0 && !g_bus_unhealthy; g_godmode_on = ok; }
        } else if (strncmp(c.text, "nocd ", 5) == 0) {
            bool enabled=false;wsm::toggle_value(c.text+5,enabled);
            feat_nocd(ack,sizeof ack,enabled?"on":"off");ok=enabled?g_nocd_on:!g_nocd_on&&!g_bus_unhealthy&&strncmp(ack,"OK ",3)==0;
        } else if (strncmp(c.text, "loot ", 5) == 0) {
            bool enabled=false;wsm::toggle_value(c.text+5,enabled);
            feat_loot(ack,sizeof ack,enabled?"on":"off");ok=enabled?g_loot_on:!g_loot_on;
        } else if (strncmp(c.text, "stunall ", 8) == 0) {
            bool enabled=false;wsm::toggle_value(c.text+8,enabled);
            feat_stunall(ack,sizeof ack,enabled?"on":"off");ok=enabled?g_stunall_on:!g_stunall_on&&!g_bus_unhealthy&&strncmp(ack,"OK ",3)==0;
        } else {
            ctl_exec(c.text, ack, sizeof ack);
            ok = strncmp(ack, "OK ", 3) == 0 || (strncmp(ack, "TP ", 3) == 0 && strstr(ack, "fault=0") && strstr(ack, "ok=1")) ||
                (strcmp(c.text, "sweep") == 0 && strncmp(ack, "MSWEEP ", 7) == 0 && strstr(ack, "fault=0"));
        }
        BEAT_END();
        if(deferred && faults==g_guard_faults){continue;}
        if (faults != g_guard_faults) {
            g_session_fault = true;
            modern_reset(ack, sizeof ack);
            g_runtime.complete(c, wsm::Outcome::Fault, "native fault; session suspended, restart target to recover");
        } else if (g_bus_unhealthy || (strcmp(c.text, "panic") == 0 && !ok)) {
            g_session_fault = true;
            g_runtime.complete(c, wsm::Outcome::Fault, ack);
        } else g_runtime.complete(c, ok ? wsm::Outcome::Applied : wsm::Outcome::Rejected, ack);
    }
}
static void modern_tick() {
    main_sweep_poll();
    if(main_sweep_pending()){modern_commands();return;}
    const sig_atomic_t tick_faults=g_guard_faults;
    static uint64_t scene_due=0;
    bool active=g_godmode_on||g_speed_on||g_nocd_on||g_loot_on||g_stunall_on||g_critdmg_on;
    for(int i=0;i<FEAT_COUNT;++i)active=active||g_feats[i].on;
    const bool foreground=__atomic_load_n(&g_foreground,__ATOMIC_ACQUIRE);
    const uint64_t current=now_ms();
    if(foreground&&!g_session_fault&&current<scene_due){modern_commands();return;}
    scene_due=current+(active?250:1000); // Reduce managed allocations while OFF/lobby.
    uint64_t hero = 0, stage = 0;
    bool scene_observed = false;
    if (__atomic_load_n(&g_identity_ok, __ATOMIC_ACQUIRE) && __atomic_load_n(&g_foreground, __ATOMIC_ACQUIRE) && !g_session_fault) {
        scene_observed = true;
        BEAT_BEGIN();
        hero = engine_find_hero_stats();
        if (g_m_stage_inst) { void *exc = nullptr; fn_inv_t2 inv = feat_inv();
            if (inv) { GUARDED_BEGIN(); stage = (uint64_t)(uintptr_t)inv(g_m_stage_inst, nullptr, nullptr, &exc); GUARDED_END(); }
            if (exc || !inv) scene_observed = false;
        }
        BEAT_END();
    }
    if (scene_observed && (hero != g_scene_hero || stage != g_scene_stage)) {
        ++g_option_lifetime; // Previous-scene keys cannot authorize managed calls.
        const uint64_t scene_epoch=g_runtime.invalidate();
        if (g_scene_hero || wsm::owned_option_objects(g_owned_options,64) || g_timescale_owned) {
            char tmp[512]; modern_reset(tmp, sizeof tmp);
            if (strncmp(tmp,"OK ",3)) ELOGI("LIFECYCLE cleanup: %s",tmp);
        }
        g_scene_hero = hero; g_scene_stage = stage; g_scratched_n = 0; g_scratch_reset = 1;
        g_control_epoch=scene_epoch;
        g_feat_version++; // Same character count can hide a completely new stage/player.
        ELOGI("LIFECYCLE epoch=%llu stage=%llx hero=%llx", (unsigned long long)scene_epoch,
              (unsigned long long)stage, (unsigned long long)hero);
    }
    modern_commands();
    if (g_scene_hero && __atomic_load_n(&g_foreground, __ATOMIC_ACQUIRE) && !g_session_fault) {
        if (g_loot_on&&!main_sweep_pending()) { BEAT_BEGIN(); loot_beat(); BEAT_END(); }
    }
    if (g_guard_faults!=tick_faults && !g_session_fault) {
        g_session_fault=true;char tmp[512];modern_reset(tmp,sizeof tmp);
        ELOGI("runtime fault: automatic work suspended until target restart");
    }
}
