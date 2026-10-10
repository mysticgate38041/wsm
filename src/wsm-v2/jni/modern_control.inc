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
    if (g_fov_owned) {
        char tf[160];
        feat_fov(tf, sizeof tf, "0");
        ELOGI("FOV reset -> %s", tf);
    }
    g_scratched_n = 0; g_scratch_reset = 1;
    g_control_epoch=reset_epoch;
    g_feature_flags.reset(reset_epoch);
}
/* ---- GM group (v6.4): composition over existing verified controls.
   No new engine paths: every operation calls the identical production handler
   as its standalone command. Presets are memory-only (no disk artifacts). ---- */
struct GmPreset {
    char name[17];
    bool used;
    bool f_on[FEAT_COUNT];
    float f_val[FEAT_COUNT];
    bool godmode, nocd, loot, stunall, speed_on, critdmg_on;
    float speed_val, critdmg_val;
};
static GmPreset g_gm_presets[8];
static const char *kGmFixed[] = {"god", "hp", "stam", "mana", "poise", "immune", "ohk", "crit", "onehp"};
static const char *kGmSlider[] = {"aura", "dmg", "timescale"};
static int gm_feat_index(const char *id) {
    for (int i = 0; i < FEAT_COUNT; ++i) if (strcmp(id, g_feats[i].id) == 0) return i;
    return -1;
}
static bool gm_apply_feat(const char *id, float value, char *ack, size_t cap) {
    char cmd[64];
    snprintf(cmd, sizeof cmd, "feat %s %.2f", id, (double)value);
    ctl_exec(cmd, ack, cap);
    return strncmp(ack, "OK", 2) == 0;
}
static bool gm_apply_speed(float value, char *ack, size_t cap) {
    char arg[32];
    snprintf(arg, sizeof arg, "%.2f", (double)value);
    feat_speed(ack, cap, arg);
    /* v6.4.1 fix: the ON success ack is "SPEED ON ..." (not "OK"); classify on
       the resulting engine state instead of the ack prefix so `gm max speed`
       and preset load with speed ON are not mislabeled rejected. */
    if (strncmp(ack, "SPEED", 5) != 0 && strncmp(ack, "OK", 2) != 0) return false;
    if (value == 0.0f) return g_speed_on == 0;
    return g_speed_on != 0 && fabsf(g_speed_value - value) < 0.05f;
}
static bool gm_apply_critdmg(float value, char *ack, size_t cap) {
    feat_critdmg(ack, cap, value);
    return strncmp(ack, "OK", 2) == 0;
}
static bool gm_apply_toggle(const char *id, bool on, char *ack, size_t cap) {
    if (strcmp(id, "godmode") == 0) {
        if (on) {
            feat_godmode(ack, cap, "");
            const bool ok = strncmp(ack, "GOD armed ", 10) == 0 && g_h64_bus[69] > 0 &&
                            (int64_t)g_h64_bus[68] == 0 && !g_bus_unhealthy;
            g_godmode_on = ok;
            return ok;
        }
        feat_godoff(ack, cap);
        return !g_godmode_on && !g_bus_unhealthy && strncmp(ack, "OK ", 3) == 0;
    }
    if (strcmp(id, "nocd") == 0) {
        feat_nocd(ack, cap, on ? "on" : "off");
        return on ? g_nocd_on : (!g_nocd_on && !g_bus_unhealthy && strncmp(ack, "OK ", 3) == 0);
    }
    if (strcmp(id, "loot") == 0) {
        feat_loot(ack, cap, on ? "on" : "off");
        return on ? g_loot_on : !g_loot_on;
    }
    if (strcmp(id, "stunall") == 0) {
        feat_stunall(ack, cap, on ? "on" : "off");
        return on ? g_stunall_on : (!g_stunall_on && !g_bus_unhealthy && strncmp(ack, "OK ", 3) == 0);
    }
    return false;
}
static bool gm_max(const char *id, char *ack, size_t cap) {
    for (const auto *f : kGmFixed) if (strcmp(id, f) == 0) return gm_apply_feat(id, 1.0f, ack, cap);
    if (strcmp(id, "aura") == 0) return gm_apply_feat(id, 40.0f, ack, cap);
    if (strcmp(id, "dmg") == 0) return gm_apply_feat(id, 99.0f, ack, cap);
    if (strcmp(id, "timescale") == 0) return gm_apply_feat(id, 5.0f, ack, cap);
    if (strcmp(id, "speed") == 0) return gm_apply_speed(5.0f, ack, cap);
    if (strcmp(id, "critdmg") == 0) return gm_apply_critdmg(5.0f, ack, cap);
    snprintf(ack, cap, "ERR unknown gm target");
    return false;
}
static bool gm_all(bool on, char *ack, size_t cap) {
    int done = 0, total = 0;
    char tmp[256] = {};
    auto step = [&](bool good) { ++total; if (good) ++done; };
    step(gm_apply_toggle("godmode", on, tmp, sizeof tmp));
    for (const auto *f : kGmFixed) step(gm_apply_feat(f, on ? 1.0f : 0.0f, tmp, sizeof tmp));
    step(gm_apply_feat("dmg", on ? 99.0f : 0.0f, tmp, sizeof tmp));
    step(gm_apply_toggle("nocd", on, tmp, sizeof tmp));
    const bool ok = done == total;
    snprintf(ack, cap, "%s GM all %s (%d/%d ops applied)", ok ? "OK" : "ERR", on ? "ON" : "OFF", done, total);
    return ok;
}
static bool gm_preset_save(const char *name, char *ack, size_t cap) {
    int slot = -1, free_slot = -1;
    for (int i = 0; i < 8; ++i) {
        if (g_gm_presets[i].used && strcmp(g_gm_presets[i].name, name) == 0) { slot = i; break; }
        if (!g_gm_presets[i].used && free_slot < 0) free_slot = i;
    }
    if (slot < 0) slot = free_slot;
    if (slot < 0) { snprintf(ack, cap, "ERR gm preset store full (8/8)"); return false; }
    GmPreset &p = g_gm_presets[slot];
    memset(&p, 0, sizeof p);
    snprintf(p.name, sizeof p.name, "%s", name);
    for (const auto *f : kGmFixed) { const int i = gm_feat_index(f); if (i >= 0) { p.f_on[i] = g_feats[i].on; p.f_val[i] = g_feats[i].value; } }
    for (const auto *f : kGmSlider) { const int i = gm_feat_index(f); if (i >= 0) { p.f_on[i] = g_feats[i].on; p.f_val[i] = g_feats[i].value; } }
    p.godmode = g_godmode_on; p.nocd = g_nocd_on != 0; p.loot = g_loot_on != 0; p.stunall = g_stunall_on != 0;
    p.speed_on = g_speed_on != 0; p.speed_val = g_speed_value;
    p.critdmg_on = g_critdmg_on; p.critdmg_val = g_critdmg_value;
    p.used = true;
    snprintf(ack, cap, "OK GM preset '%s' saved (slot %d)", name, slot);
    return true;
}
static bool gm_preset_load(const char *name, char *ack, size_t cap) {
    GmPreset *p = nullptr;
    for (auto &s : g_gm_presets) if (s.used && strcmp(s.name, name) == 0) { p = &s; break; }
    if (!p) { snprintf(ack, cap, "ERR gm preset '%s' not found", name); return false; }
    int done = 0, total = 0;
    char tmp[256] = {};
    auto step = [&](bool good) { ++total; if (good) ++done; };
    for (const auto *f : kGmFixed) { const int i = gm_feat_index(f); if (i < 0) continue; step(gm_apply_feat(f, p->f_on[i] ? 1.0f : 0.0f, tmp, sizeof tmp)); }
    for (const auto *f : kGmSlider) { const int i = gm_feat_index(f); if (i < 0) continue; step(gm_apply_feat(f, p->f_on[i] ? p->f_val[i] : 0.0f, tmp, sizeof tmp)); }
    step(gm_apply_toggle("godmode", p->godmode, tmp, sizeof tmp));
    step(gm_apply_toggle("nocd", p->nocd, tmp, sizeof tmp));
    step(gm_apply_toggle("loot", p->loot, tmp, sizeof tmp));
    step(gm_apply_toggle("stunall", p->stunall, tmp, sizeof tmp));
    step(gm_apply_speed(p->speed_on ? p->speed_val : 0.0f, tmp, sizeof tmp));
    step(gm_apply_critdmg(p->critdmg_on ? p->critdmg_val : 0.0f, tmp, sizeof tmp));
    const bool ok = done == total;
    snprintf(ack, cap, "%s GM preset '%s' load (%d/%d ops applied)", ok ? "OK" : "ERR", name, done, total);
    return ok;
}
static void gm_status(char *ack, size_t cap) {
    int presets = 0;
    char names[128] = {};
    for (auto &s : g_gm_presets) if (s.used) {
        ++presets;
        if (names[0]) strncat(names, ",", sizeof names - strlen(names) - 1);
        strncat(names, s.name, sizeof names - strlen(names) - 1);
    }
    snprintf(ack, cap,
             "OK GM godmode:%d nocd:%d loot:%d stunall:%d speed:%d/%.2f critdmg:%d/%.2f "
             "| god:%d hp:%d stam:%d mana:%d poise:%d immune:%d ohk:%d crit:%d onehp:%d "
             "| aura:%d/%.0f dmg:%d/%.0f timescale:%d/%.2f | presets:%d[%s]",
             g_godmode_on ? 1 : 0, g_nocd_on ? 1 : 0, g_loot_on ? 1 : 0, g_stunall_on ? 1 : 0,
             (int)(g_speed_on != 0), (double)g_speed_value, g_critdmg_on ? 1 : 0, (double)g_critdmg_value,
             g_feats[FEAT_GOD].on ? 1 : 0, g_feats[FEAT_HP].on ? 1 : 0, g_feats[FEAT_STAM].on ? 1 : 0,
             g_feats[FEAT_MANA].on ? 1 : 0, g_feats[FEAT_POISE].on ? 1 : 0, g_feats[FEAT_IMMUNE].on ? 1 : 0,
             g_feats[FEAT_OHK].on ? 1 : 0, g_feats[FEAT_CRIT].on ? 1 : 0, g_feats[FEAT_ONEHP].on ? 1 : 0,
             g_feats[FEAT_AURA].on ? 1 : 0, (double)g_feats[FEAT_AURA].value,
             g_feats[FEAT_DMG].on ? 1 : 0, (double)g_feats[FEAT_DMG].value,
             g_feats[FEAT_TIMESCALE].on ? 1 : 0, (double)g_feats[FEAT_TIMESCALE].value,
             presets, names);
}
/* v6.4 GM: absolute waypoints. Slots store world (x,y,z); load reuses the
   production teleport path (absolute mode) with its own read-back proof. */
static float g_gm_pos[8][3];
static bool g_gm_pos_used[8];
static bool gm_pos_save(int slot, char *ack, size_t cap) {
    if (slot < 0 || slot > 7) { snprintf(ack, cap, "ERR gm pos slot 0..7"); return false; }
    float p[3];
    if (!gm_hero_pos(p)) { snprintf(ack, cap, "ERR gm pos: hero tidak ditemukan"); return false; }
    g_gm_pos[slot][0] = p[0]; g_gm_pos[slot][1] = p[1]; g_gm_pos[slot][2] = p[2];
    g_gm_pos_used[slot] = true;
    snprintf(ack, cap, "OK GM pos #%d saved (x=%.1f z=%.1f)", slot, (double)p[0], (double)p[2]);
    return true;
}
static bool gm_pos_load(int slot, char *ack, size_t cap) {
    if (slot < 0 || slot > 7 || !g_gm_pos_used[slot]) {
        snprintf(ack, cap, "ERR gm pos #%d kosong (slot 0..7)", slot); return false;
    }
    char tmp[256];
    feat_teleport(g_gm_pos[slot][0], g_gm_pos[slot][2], 1, tmp, sizeof tmp);
    const bool ok = strstr(tmp, "ok=1") != nullptr;
    snprintf(ack, cap, "%s GM pos #%d -> (%.1f,%.1f) | %s", ok ? "OK" : "ERR", slot,
             (double)g_gm_pos[slot][0], (double)g_gm_pos[slot][2], tmp);
    return ok;
}
static void gm_pos_list(char *ack, size_t cap) {
    int n = 0;
    char body[192] = {};
    for (int i = 0; i < 8; ++i) if (g_gm_pos_used[i]) {
        ++n;
        char one[40];
        snprintf(one, sizeof one, "#%d(%.1f,%.1f) ", i, (double)g_gm_pos[i][0], (double)g_gm_pos[i][2]);
        strncat(body, one, sizeof body - strlen(body) - 1);
    }
    snprintf(ack, cap, "OK GM pos %d/8 %s", n, n ? body : "-");
}
static bool modern_gm(const char *body, char *ack, size_t cap) {
    if (strcmp(body, "reset") == 0) {
        char tmp[512];
        modern_reset(tmp, sizeof tmp);
        const bool ok = !g_bus_unhealthy && strncmp(tmp, "OK ", 3) == 0;
        snprintf(ack, cap, "%s GM reset (%s)", ok ? "OK" : "ERR",
                 ok ? "all controls off; slider values retained" : tmp);
        return ok;
    }
    if (strcmp(body, "status") == 0) { gm_status(ack, cap); return true; }
    if (strncmp(body, "max ", 4) == 0) return gm_max(body + 4, ack, cap);
    if (strncmp(body, "all ", 4) == 0) { bool on = false; wsm::toggle_value(body + 4, on); return gm_all(on, ack, cap); }
    if (strncmp(body, "pos ", 4) == 0) {
        const char *p = body + 4;
        if (strcmp(p, "list") == 0) { gm_pos_list(ack, cap); return true; }
        int slot = -1; char tail;
        if (sscanf(p, "save %d %c", &slot, &tail) == 1) return gm_pos_save(slot, ack, cap);
        if (sscanf(p, "load %d %c", &slot, &tail) == 1) return gm_pos_load(slot, ack, cap);
    }
    if (strncmp(body, "preset ", 7) == 0) {
        const char *p = body + 7;
        if (strcmp(p, "list") == 0) {
            char names[128] = {};
            int n = 0;
            for (auto &s : g_gm_presets) if (s.used) {
                ++n;
                if (names[0]) strncat(names, ",", sizeof names - strlen(names) - 1);
                strncat(names, s.name, sizeof names - strlen(names) - 1);
            }
            snprintf(ack, cap, "OK GM presets %d/8 [%s]", n, names);
            return true;
        }
        if (strncmp(p, "save ", 5) == 0) return gm_preset_save(p + 5, ack, cap);
        if (strncmp(p, "load ", 5) == 0) return gm_preset_load(p + 5, ack, cap);
    }
    snprintf(ack, cap, "ERR unknown gm command");
    return false;
}
static void modern_commands() {
    wsm::Command c{};
    // Limit command work per beat so continuous producers cannot starve updates.
    for (int budget = 0; budget < 4 && g_runtime.pop(c); ++budget) {
        char ack[512] = "ERR execution fault";
        bool ok = false;
        if(main_sweep_pending()) {
            if((strcmp(c.text,"panic")==0||strcmp(c.text,"gm reset")==0) && !g_main_reset_command.id){main_sweep_cancel();g_main_reset_command=c;}
            else g_runtime.complete(c,wsm::Outcome::Rejected,"UnityMain sweep in progress; retry after completion");
            continue;
        }
        const bool reset_like = strcmp(c.text, "panic") == 0 || strcmp(c.text, "gm reset") == 0;
        if (!reset_like && c.epoch != g_runtime.epoch()) {
            g_runtime.complete(c, wsm::Outcome::Stale, "scene/activity changed; submit again"); continue;
        }
        if (!reset_like &&
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
        } else if (strncmp(c.text, "fov ", 4) == 0) {
            float v = strtof(c.text + 4, nullptr); feat_fov(ack, sizeof ack, c.text + 4);
            ok = strncmp(ack, "FOV ", 4) == 0 && strstr(ack, "fault=0") && (v == 0 ? !g_fov_owned : g_fov_owned);
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
        } else if (strncmp(c.text, "gm ", 3) == 0) {
            ok = modern_gm(c.text + 3, ack, sizeof ack);
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
        if (g_scene_hero || wsm::owned_option_objects(g_owned_options,64) || g_timescale_owned || g_fov_owned) {
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
