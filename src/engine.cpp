#include "app.h"
#include <cstdio>
#include <cstdarg>
#include <cmath>

Engine g_engine;
void device_autobind(Device* dev, DeviceProfile* dp);

void Engine::init(){
    InitializeSRWLock(&lock_);
    InitializeCriticalSection(&log_cs_);
    t0_ = qpc_now();
    log("engine ready");
}
void Engine::shutdown(){
    midi.close();
}

void Engine::log(const char* fmt, ...){
    LogLine l;
    l.t = qpc_to_ms(qpc_now() - t0_) / 1000.0;
    va_list ap; va_start(ap, fmt);
    vsnprintf(l.text, sizeof l.text, fmt, ap);
    va_end(ap);
    EnterCriticalSection(&log_cs_);
    log_[log_head_] = l;
    log_head_ = (log_head_ + 1) % 128;
    if (log_count_ < 128) log_count_++;
    LeaveCriticalSection(&log_cs_);
}
int Engine::log_copy(LogLine* out, int max){
    EnterCriticalSection(&log_cs_);
    int n = log_count_ < max ? log_count_ : max;
    for (int i = 0; i < n; i++){
        int idx = (log_head_ - 1 - i + 256) % 128;
        out[i] = log_[idx];
    }
    LeaveCriticalSection(&log_cs_);
    return n;
}

void Engine::publish(const Profile& p){
    AcquireSRWLockExclusive(&lock_);
    prof_ = p;
    ReleaseSRWLockExclusive(&lock_);
}
void Engine::snapshot(Profile& out){
    AcquireSRWLockShared(&lock_);
    out = prof_;
    ReleaseSRWLockShared(&lock_);
}
void Engine::panic(){
    midi.panic();
    transpose_now.store(0);
    layer_now.store(0);
    log("panic - all notes off");
}
void Engine::set_midi_port(int index){
    if (index < 0){ midi.close(); log("midi output closed"); return; }
    if (midi.open(index)){
        auto ports = midi_list_outputs();
        const char* nm = "?";
        for (auto& p : ports) if (p.index == index) nm = p.name.c_str();
        log("midi out: %s", nm);
    } else log("could not open midi port %d", index);
}

// ---------------------------------------------------------------- helpers ----
static inline float apply_dz(float v, float dz){
    float a = v < 0 ? -v : v;
    if (a <= dz) return 0.f;
    float s = (a - dz) / (1.f - dz);
    return v < 0 ? -s : s;
}
static inline bool hat_hit(int hat, int dir){
    if (hat < 0) return false;
    return hat == dir || hat == ((dir + 1) & 7) || hat == ((dir + 7) & 7);
}

void Engine::normalise(Device* d, const DeviceProfile* dp, const RawSample& raw, NormState& ns){
    ns.buttons = 0;
    for (int i = 0; i < C_BTN_COUNT; i++){
        const HwBind& b = dp->btn[i];
        bool on = false;
        if (b.kind == HW_BUTTON && b.index >= 0 && b.index < 32) on = (raw.buttons >> b.index) & 1u;
        else if (b.kind == HW_HAT) on = hat_hit(raw.hat, b.index);
        if (on) ns.buttons |= 1u << i;
    }
    for (int a = 0; a < AX_COUNT; a++){
        const HwBind& b = dp->axis[a];
        float v = 0.f;
        if (b.kind == HW_AXIS && b.index >= 0 && b.index < raw.n_value){
            float r = raw.value[b.index];
            if (a == AX_LT || a == AX_RT) v = r;
            else                          v = r * 2.f - 1.f;
            if (dp->axis_invert[a]) v = (a == AX_LT || a == AX_RT) ? (1.f - v) : -v;
            v = apply_dz(v, dp->axis_deadzone[a]);
        }
        ns.axis[a] = v;
    }
}

// ------------------------------------------------------------------- fire ----
void Engine::fire(Device* d, int ctrl, const Action& a, bool down, float analog){
    CtrlRT& rt = d->rt[ctrl];
    if (a.kind == ACT_KEY && !prof_.keys_enabled) return;
    if (a.kind != ACT_KEY && a.kind != ACT_LAYER && a.kind != ACT_TRANSPOSE &&
        a.kind != ACT_PANIC && !prof_.midi_enabled) return;
    switch (a.kind){
    case ACT_NOTE: {
        int tr = transpose_now.load(std::memory_order_relaxed);
        if (a.latch){
            if (!down) return;
            if (rt.latched){ if (rt.held_note >= 0) midi.note_off(rt.held_chan, rt.held_note); rt.latched = false; rt.held_note = -1; }
            else {
                int n = a.number + tr;
                if (n < 0) n = 0;
            if (n > 127) n = 127;
                int v = a.velocity;
                if (analog > 0.f) v = (int)(analog * 126.f) + 1;
                midi.note_on(a.channel, n, v);
                rt.held_note = n; rt.held_chan = a.channel; rt.latched = true;
            }
            events.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (down){
            int n = a.number + tr;
            if (n < 0) n = 0;
            if (n > 127) n = 127;
            int v = a.velocity;
            if (analog > 0.f) v = (int)(analog * 126.f) + 1;
            midi.note_on(a.channel, n, v);
            rt.held_note = n; rt.held_chan = a.channel;
        } else if (rt.held_note >= 0){
            midi.note_off(rt.held_chan, rt.held_note);
            rt.held_note = -1;
        }
        break;
    }
    case ACT_CC:
        midi.cc(a.channel, a.number, down ? a.velocity : a.off_value);
        break;
    case ACT_PROGRAM:
        if (down) midi.program(a.channel, a.number);
        break;
    case ACT_PITCHBEND:
        midi.bend(a.channel, down ? 16383 : 8192);
        break;
    case ACT_KEY:
        key_send(a.scancode, a.extended, a.mods, down);
        break;
    case ACT_TRANSPOSE:
        if (a.latch){
            if (down){
                transpose_now.fetch_add(rt.latched ? -a.transpose : a.transpose);
                rt.latched = !rt.latched;
            }
        } else {
            transpose_now.fetch_add(down ? a.transpose : -a.transpose);
        }
        break;
    case ACT_LAYER:
        layer_now.store(down ? 1 : 0, std::memory_order_relaxed);
        break;
    case ACT_PANIC:
        if (down) panic();
        break;
    default: return;
    }
    events.fetch_add(1, std::memory_order_relaxed);
}

void Engine::analog_update(Device* d, int ctrl, const Action& a, float v, uint64_t stamp){
    CtrlRT& rt = d->rt[ctrl];
    if (!prof_.midi_enabled) return;
    float t;
    if (a.bipolar){
        // full travel across the output range, centre sits in the middle
        t = (apply_dz(v, a.deadzone) + 1.f) * 0.5f;
    } else {
        // only the push away from rest counts, so centre reads as zero
        float m = v > 0.f ? v : 0.f;
        t = (m <= a.deadzone) ? 0.f : (m - a.deadzone) / (1.f - a.deadzone);
    }
    if (a.invert) t = 1.f - t;
    if (a.curve != 1.0f && t > 0.f) t = powf(t, a.curve);
    t = a.out_min + t * (a.out_max - a.out_min);
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;

    int out;
    if (a.kind == ACT_PITCHBEND) out = (int)(t * 16383.f + 0.5f);
    else                         out = (int)(t * 127.f + 0.5f);
    if (out == rt.last_out) return;

    // Value-change filtering already caps us at 128 (or 16384) steps; the gap
    // keeps a jittering stick from flooding the port on a 1 kHz pad.
    if (rt.last_send && a.kind != ACT_PITCHBEND){
        double gap_us = qpc_to_ms(stamp - rt.last_send) * 1000.0;
        int need = prof_.cc_min_gap_us;
        if (gap_us < need) return;
    }
    rt.last_out = out;
    rt.last_send = stamp;

    switch (a.kind){
    case ACT_CC:        midi.cc(a.channel, a.number, out); break;
    case ACT_PITCHBEND: midi.bend(a.channel, out); break;
    case ACT_PROGRAM:   midi.program(a.channel, out); break;
    default: return;
    }
    events.fetch_add(1, std::memory_order_relaxed);
}

// -------------------------------------------------------------- hot path -----
void Engine::on_report(Device* d, const RawSample& raw, uint64_t stamp){
    AcquireSRWLockShared(&lock_);

    const DeviceProfile* dp = nullptr;
    if (d->prof_index >= 0 && d->prof_index < (int)prof_.devices.size() &&
        prof_.devices[d->prof_index].key == d->key){
        dp = &prof_.devices[d->prof_index];
    } else {
        for (size_t i = 0; i < prof_.devices.size(); i++)
            if (prof_.devices[i].key == d->key){ dp = &prof_.devices[i]; d->prof_index = (int)i; break; }
    }
    if (!dp){ ReleaseSRWLockShared(&lock_); return; }

    NormState ns;
    normalise(d, dp, raw, ns);
    NormState prev = d->last_norm;
    d->last_norm = ns;

    if (!active() || learn_ctrl.load(std::memory_order_relaxed) >= 0){
        ReleaseSRWLockShared(&lock_);
        return;
    }

    const int layer = layer_now.load(std::memory_order_relaxed);
    uint32_t changed = ns.buttons ^ prev.buttons;

    // digital buttons
    while (changed){
        int i = 0;
        uint32_t bit = changed & (~changed + 1u);
        while ((bit >> i) != 1u) i++;
        changed &= ~bit;
        bool down = (ns.buttons & bit) != 0;
        CtrlRT& rt = d->rt[i];
        int L = down ? layer : (rt.press_layer >= 0 ? rt.press_layer : layer);
        const Action& a = prof_.layer[L][i];
        if (a.assigned()){
            float vel = -1.f;
            if (down && a.kind == ACT_NOTE && a.vel_from >= 0 && a.vel_from < AX_COUNT){
                float s = ns.axis[a.vel_from];
                vel = s < 0 ? -s : s;
            }
            fire(d, i, a, down, vel);
        }
        rt.press_layer = down ? layer : -1;
    }

    // continuous axes and axis-as-button
    for (int ax = 0; ax < AX_COUNT; ax++){
        float v = ns.axis[ax];
        if (v == prev.axis[ax]) continue;
        int cA = C_AX_BEGIN + ax;
        const Action& a = prof_.layer[layer][cA];
        if (a.assigned()){
            if (a.kind == ACT_CC || a.kind == ACT_PITCHBEND || a.kind == ACT_PROGRAM)
                analog_update(d, cA, a, v, stamp);
            else {
                float mag = v < 0 ? -v : v;
                bool on = mag > a.threshold;
                CtrlRT& rt = d->rt[cA];
                if (on != rt.dir_down){ rt.dir_down = on; fire(d, cA, a, on, on ? mag : -1.f); }
            }
        }
    }
    for (int dslot = 0; dslot < DIR_COUNT; dslot++){
        int c = C_DIR_BEGIN + dslot;
        int ax, sign;
        dir_decode(c, &ax, &sign);
        float v = ns.axis[ax] * (float)sign;
        float pv = prev.axis[ax] * (float)sign;
        CtrlRT& rt = d->rt[c];
        const Action& a0 = prof_.layer[0][c];
        const Action& a1 = prof_.layer[1][c];
        if (!a0.assigned() && !a1.assigned()){ (void)pv; continue; }
        const Action& a = prof_.layer[rt.dir_down && rt.press_layer >= 0 ? rt.press_layer : layer][c];
        float th = a.threshold > 0.01f ? a.threshold : 0.5f;
        bool on = rt.dir_down ? (v > th * 0.75f) : (v > th);     // hysteresis
        if (on != rt.dir_down){
            rt.dir_down = on;
            if (a.assigned()) fire(d, c, a, on, on ? v : -1.f);
            rt.press_layer = on ? layer : -1;
        }
    }

    ReleaseSRWLockShared(&lock_);

    uint32_t us = (uint32_t)(qpc_to_ms(qpc_now() - stamp) * 1000.0);
    if (us > worst_us.load(std::memory_order_relaxed)) worst_us.store(us, std::memory_order_relaxed);
}
