// Production entry points. Legacy experimental commands are not exposed by this dispatcher.
static void json_quote(const char *s, char *out, size_t cap) {
    size_t n = 0;
    for (; *s && n + 3 < cap; ++s) {
        unsigned char c = static_cast<unsigned char>(*s);
        if (c == '"' || c == '\\') out[n++] = '\\';
        out[n++] = c < 32 ? ' ' : static_cast<char>(c);
    }
    out[n] = 0;
}
static bool modern_valid(const char *c) {
    if (strcmp(c, "panic") == 0 || strcmp(c, "sweep") == 0 || strcmp(c, "selftest") == 0) return true;
    char id[48], extra;
    float v = 0, z = 0;
    if (sscanf(c, "feat %47s %f %c", id, &v, &extra) == 2) {
        if (!isfinite(v) || v < 0) return false;
        if (strcmp(id, "aura") == 0) return v == 0 || wsm::finite_range(v, 5, 40);
        if (strcmp(id, "dmg") == 0) return v == 0 || wsm::finite_range(v, 1, 99);
        if (strcmp(id, "timescale") == 0) return v == 0 || wsm::finite_range(v, .1f, 5);
        for (int i = 0; i < FEAT_COUNT; ++i)
            if (strcmp(id, g_feats[i].id) == 0 && i != FEAT_AGGRO && i != FEAT_STUN) return v == 0 || v == 1;
        return false;
    }
    if (sscanf(c, "speed %f %c", &v, &extra) == 1) return v == 0 || wsm::finite_range(v, 1, 5);
    if (sscanf(c, "critdmg %f %c", &v, &extra) == 1) return v == 0 || wsm::finite_range(v, 1, 5);
    if (sscanf(c, "tpr %f %f %c", &v, &z, &extra) == 2)
        return wsm::finite_range(v, -100, 100) && wsm::finite_range(z, -100, 100);
    if (sscanf(c, "%47s %f %c", id, &v, &extra) == 2 && (v == 0 || v == 1))
        return strcmp(id, "godmode") == 0 || strcmp(id, "nocd") == 0 || strcmp(id, "loot") == 0 || strcmp(id, "stunall") == 0;
    return false;
}
static void modern_request(const char *c, char *out, size_t n, uint64_t expected_epoch) {
    if (strncmp(c, "epoch ", 6) == 0) {
        unsigned long long epoch = 0; int body = 0;
        if (expected_epoch || sscanf(c + 6, "%llu %n", &epoch, &body) != 1 || !epoch || !body || !c[6 + body]) {
            snprintf(out, n, "{\"state\":\"rejected\",\"detail\":\"invalid expected epoch\"}"); return;
        }
        modern_request(c + 6 + body, out, n, epoch); return;
    }
    if (strcmp(c, "status") == 0 || strcmp(c, "telemetry") == 0) { g_runtime.snapshot(out, n); return; }
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
static void modern_publish() {
    char out[4096];
    const bool ready = __atomic_load_n(&g_identity_ok, __ATOMIC_ACQUIRE) &&
        __atomic_load_n(&g_foreground, __ATOMIC_ACQUIRE) && g_scene_hero && !g_session_fault;
    const char *state = g_session_fault ? "fault" : !__atomic_load_n(&g_identity_ok, __ATOMIC_ACQUIRE) ? "identity_pending" :
        !__atomic_load_n(&g_foreground, __ATOMIC_ACQUIRE) ? "paused" : !g_scene_hero ? "scene_pending" : "ready";
    size_t used = snprintf(out, sizeof out,
        "{\"build\":\"%s\",\"state\":\"%s\",\"ready\":%s,\"pid\":%d,\"epoch\":%llu,\"faults\":%d,"
        "\"payload\":%s,\"players\":%d,\"lootCandidates\":%d,\"lootRequested\":%d,\"observed\":null,\"features\":{",
        WSM_BUILD_STAMP, state, ready ? "true" : "false", getpid(), (unsigned long long)g_runtime.epoch(),
        (int)g_guard_faults, g_h64_handle && !g_bus_unhealthy ? "true" : "false", g_last_char_count,
        g_loot_last_n, g_loot_last_done);
    for (int i = 0; i < FEAT_COUNT; ++i) {
        if (i == FEAT_STUN || i == FEAT_AGGRO) continue; // retired entries; no duplicate JSON keys
        used += snprintf(out + used, sizeof out - used, "%s\"%s\":{\"on\":%s,\"value\":%.3f}",
                         i ? "," : "", g_feats[i].id, g_feats[i].on ? "true" : "false", (double)g_feats[i].value);
    }
    snprintf(out + used, sizeof out - used,
        ",\"godmode\":{\"on\":%s,\"value\":1},\"speed\":{\"on\":%s,\"value\":%.2f},"
        "\"nocd\":{\"on\":%s,\"value\":1},\"loot\":{\"on\":%s,\"value\":1},\"stunall\":{\"on\":%s,\"value\":1},\"critdmg\":{\"on\":%s,\"value\":%.2f}}}",
        g_godmode_on ? "true" : "false", g_speed_on ? "true" : "false", (double)g_speed_value,
        g_nocd_on ? "true" : "false", g_loot_on ? "true" : "false", g_stunall_on ? "true" : "false",
        g_critdmg_on ? "true" : "false", (double)g_critdmg_value);
    g_runtime.publish(out);
}
static void modern_reset(char *out, size_t cap) {
    ctl_exec("panic", out, cap);
    g_godmode_on = false;
    g_scratched_n = 0; g_scratch_reset = 1;
}
static void modern_commands() {
    wsm::Command c{};
    // Limit command work per beat so continuous producers cannot starve updates.
    for (int budget = 0; budget < 4 && g_runtime.pop(c); ++budget) {
        char ack[512] = "ERR execution fault";
        bool ok = false;
        if (strcmp(c.text, "panic") != 0 && c.epoch != g_runtime.epoch()) {
            g_runtime.complete(c, wsm::Outcome::Stale, "scene/activity changed; submit again"); continue;
        }
        if (strcmp(c.text, "panic") != 0 &&
            (!__atomic_load_n(&g_identity_ok, __ATOMIC_ACQUIRE) ||
             !__atomic_load_n(&g_foreground, __ATOMIC_ACQUIRE) || !g_scene_hero || g_session_fault)) {
            g_runtime.complete(c, wsm::Outcome::Rejected, "session not ready (identity, scene, foreground or fault)"); continue;
        }
        const sig_atomic_t faults = g_guard_faults;
        BEAT_BEGIN();
        if (strcmp(c.text, "panic") == 0) {
            modern_reset(ack, sizeof ack); ok = !g_bus_unhealthy && strncmp(ack, "OK ", 3) == 0;
        } else if (strcmp(c.text, "selftest") == 0) {
            if (hook_ensure_payload()) {
                uint64_t tok = bus_arm();
                if (tok) {
                    g_h64_bus[4] = 6; g_h64_bus[5] = 7;
                    __atomic_store_n(&g_h64_bus[0], 2ULL, __ATOMIC_RELEASE);
                    ok = bus_done(tok) && g_h64_bus[6] == 42;
                }
            }
            if(ok) {
                uint64_t tok=bus_arm();ok=false;
                if(tok){__atomic_store_n(&g_h64_bus[0],16ULL,__ATOMIC_RELEASE);ok=bus_done(tok)&&g_h64_bus[6]==42&&(int64_t)g_h64_bus[68]==0;}
            }
            snprintf(ack, sizeof ack, "%s payload multiply=42 branch/hero-getter/god/neighbor/restore", ok ? "OK" : "ERR");
        } else if (strncmp(c.text, "speed ", 6) == 0) {
            float v = strtof(c.text + 6, nullptr); feat_speed(ack, sizeof ack, c.text + 6);
            ok = v == 0 ? !g_speed_on && !g_bus_unhealthy && strncmp(ack, "OK ", 3) == 0 : g_speed_on;
        } else if (strncmp(c.text, "critdmg ", 8) == 0) {
            float v = strtof(c.text + 8, nullptr); feat_critdmg(ack, sizeof ack, v);
            ok = strncmp(ack, "OK ", 3) == 0 && !g_bus_unhealthy && (v == 0 ? !g_critdmg_on : g_critdmg_on);
        } else if (strncmp(c.text, "godmode ", 8) == 0) {
            if (c.text[8] == '0') { feat_godoff(ack, sizeof ack); ok = !g_godmode_on && !g_bus_unhealthy && strncmp(ack, "OK ", 3) == 0; }
            else if(g_godmode_on){ok=true;snprintf(ack,sizeof ack,"OK GOD already ON (owned slot unchanged)");}
            else { feat_godmode(ack, sizeof ack, ""); ok = strncmp(ack, "GOD armed ", 10) == 0 && g_h64_bus[69] > 0 && (int64_t)g_h64_bus[68] == 0 && !g_bus_unhealthy; g_godmode_on = ok; }
        } else if (strncmp(c.text, "nocd ", 5) == 0) {
            feat_nocd(ack, sizeof ack, c.text[5] == '0' ? "off" : "on"); ok = c.text[5] == '0' ? !g_nocd_on && !g_bus_unhealthy && strncmp(ack, "OK ", 3) == 0 : g_nocd_on;
        } else if (strncmp(c.text, "loot ", 5) == 0) {
            feat_loot(ack, sizeof ack, c.text[5] == '0' ? "off" : "on"); ok = c.text[5] == '0' ? !g_loot_on : g_loot_on;
        } else if (strncmp(c.text, "stunall ", 8) == 0) {
            feat_stunall(ack, sizeof ack, c.text[8] == '0' ? "off" : "on"); ok = c.text[8] == '0' ? !g_stunall_on && !g_bus_unhealthy && strncmp(ack, "OK ", 3) == 0 : g_stunall_on;
        } else {
            ctl_exec(c.text, ack, sizeof ack);
            ok = strncmp(ack, "OK ", 3) == 0 || (strncmp(ack, "TP ", 3) == 0 && strstr(ack, "fault=0") && strstr(ack, "ok=1")) ||
                (strcmp(c.text, "sweep") == 0 && strncmp(ack, "MSWEEP ", 7) == 0 && strstr(ack, "fault=0"));
        }
        BEAT_END();
        if (faults != g_guard_faults) {
            g_session_fault = true;
            modern_reset(ack, sizeof ack);
            g_runtime.complete(c, wsm::Outcome::Fault, "native fault; session suspended, restart target to recover");
        } else if (g_bus_unhealthy || (strcmp(c.text, "panic") == 0 && !ok)) {
            g_session_fault = true;
            g_runtime.complete(c, wsm::Outcome::Fault, "restoration/channel uncertain; restart target before further commands");
        } else g_runtime.complete(c, ok ? wsm::Outcome::Applied : wsm::Outcome::Rejected, ack);
        modern_publish();
    }
}
static void modern_tick() {
    const sig_atomic_t tick_faults=g_guard_faults;
    uint64_t hero = 0, stage = 0;
    if (__atomic_load_n(&g_identity_ok, __ATOMIC_ACQUIRE) && __atomic_load_n(&g_foreground, __ATOMIC_ACQUIRE) && !g_session_fault) {
        BEAT_BEGIN();
        hero = engine_find_hero_stats();
        if (g_m_stage_inst) { void *exc = nullptr; fn_inv_t2 inv = feat_inv();
            if (inv) { GUARDED_BEGIN(); stage = (uint64_t)(uintptr_t)inv(g_m_stage_inst, nullptr, nullptr, &exc); GUARDED_END(); }
        }
        BEAT_END();
    }
    if (hero != g_scene_hero || stage != g_scene_stage) {
        g_runtime.invalidate();
        if (g_scene_hero) { char tmp[512]; modern_reset(tmp, sizeof tmp); }
        memset(g_owned_options, 0, sizeof g_owned_options);
        g_scene_hero = hero; g_scene_stage = stage; g_scratched_n = 0; g_scratch_reset = 1;
        g_feat_version++; // Same character count can hide a completely new stage/player.
        ELOGI("LIFECYCLE epoch=%llu stage=%llx hero=%llx", (unsigned long long)g_runtime.epoch(),
              (unsigned long long)stage, (unsigned long long)hero);
    }
    modern_commands();
    if (g_scene_hero && __atomic_load_n(&g_foreground, __ATOMIC_ACQUIRE) && !g_session_fault) {
        if (g_loot_on) { BEAT_BEGIN(); loot_beat(); BEAT_END(); }
    }
    if (g_guard_faults!=tick_faults && !g_session_fault) {
        g_session_fault=true;char tmp[512];modern_reset(tmp,sizeof tmp);
        ELOGI("runtime fault: automatic work suspended until target restart");
    }
    modern_publish();
}
