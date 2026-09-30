#include "app.h"
#include <cstdio>

// ---------------------------------------------------------------- timing ----
static double g_qpc_to_ms = 0.0;
uint64_t qpc_now(){ LARGE_INTEGER t; QueryPerformanceCounter(&t); return (uint64_t)t.QuadPart; }
double qpc_to_ms(uint64_t ticks){
    if (g_qpc_to_ms == 0.0){
        LARGE_INTEGER f; QueryPerformanceFrequency(&f);
        g_qpc_to_ms = 1000.0 / (double)f.QuadPart;
    }
    return (double)ticks * g_qpc_to_ms;
}

// ------------------------------------------------------------------ midi ----
static CRITICAL_SECTION g_midi_cs;
static bool g_midi_cs_ready = false;
static void midi_cs_init(){ if (!g_midi_cs_ready){ InitializeCriticalSection(&g_midi_cs); g_midi_cs_ready = true; } }

std::vector<MidiPortInfo> midi_list_outputs(){
    std::vector<MidiPortInfo> v;
    UINT n = midiOutGetNumDevs();
    for (UINT i = 0; i < n; i++){
        MIDIOUTCAPSA caps = {};
        if (midiOutGetDevCapsA(i, &caps, sizeof caps) == MMSYSERR_NOERROR)
            v.push_back({ (int)i, caps.szPname });
    }
    return v;
}

bool MidiOut::open(int device_index){
    midi_cs_init();
    close();
    HMIDIOUT h = nullptr;
    MMRESULT r = midiOutOpen(&h, (UINT)device_index, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR) return false;
    EnterCriticalSection(&g_midi_cs);
    h_ = h; index_ = device_index;
    memset(active_, 0, sizeof active_);
    LeaveCriticalSection(&g_midi_cs);
    return true;
}
void MidiOut::close(){
    if (!h_) return;
    panic();
    midi_cs_init();
    EnterCriticalSection(&g_midi_cs);
    HMIDIOUT h = h_; h_ = nullptr; index_ = -1;
    LeaveCriticalSection(&g_midi_cs);
    midiOutReset(h);
    midiOutClose(h);
}
void MidiOut::send(uint32_t msg){
    if (!h_) return;
    midiOutShortMsg(h_, msg);
    sent.fetch_add(1, std::memory_order_relaxed);
}
void MidiOut::note_on(int ch, int note, int vel){
    if (note < 0 || note > 127) return;
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;
    ch = (ch - 1) & 15;
    EnterCriticalSection(&g_midi_cs);
    if (active_[ch][note] < 250) active_[ch][note]++;
    send(0x90u | ch | ((uint32_t)note << 8) | ((uint32_t)vel << 16));
    LeaveCriticalSection(&g_midi_cs);
}
void MidiOut::note_off(int ch, int note){
    if (note < 0 || note > 127) return;
    ch = (ch - 1) & 15;
    EnterCriticalSection(&g_midi_cs);
    if (active_[ch][note] > 0) active_[ch][note]--;
    if (active_[ch][note] == 0)
        send(0x80u | ch | ((uint32_t)note << 8) | (64u << 16));
    LeaveCriticalSection(&g_midi_cs);
}
void MidiOut::cc(int ch, int num, int val){
    if (num < 0 || num > 127) return;
    if (val < 0) val = 0;
    if (val > 127) val = 127;
    ch = (ch - 1) & 15;
    EnterCriticalSection(&g_midi_cs);
    send(0xB0u | ch | ((uint32_t)num << 8) | ((uint32_t)val << 16));
    LeaveCriticalSection(&g_midi_cs);
}
void MidiOut::program(int ch, int num){
    if (num < 0 || num > 127) return;
    ch = (ch - 1) & 15;
    EnterCriticalSection(&g_midi_cs);
    send(0xC0u | ch | ((uint32_t)num << 8));
    LeaveCriticalSection(&g_midi_cs);
}
void MidiOut::bend(int ch, int value14){
    if (value14 < 0) value14 = 0;
    if (value14 > 16383) value14 = 16383;
    ch = (ch - 1) & 15;
    EnterCriticalSection(&g_midi_cs);
    send(0xE0u | ch | ((uint32_t)(value14 & 0x7F) << 8) | ((uint32_t)(value14 >> 7) << 16));
    LeaveCriticalSection(&g_midi_cs);
}
void MidiOut::panic(){
    if (!h_) return;
    midi_cs_init();
    EnterCriticalSection(&g_midi_cs);
    for (int ch = 0; ch < 16; ch++){
        for (int n = 0; n < 128; n++)
            if (active_[ch][n]){ send(0x80u | ch | ((uint32_t)n << 8) | (64u << 16)); active_[ch][n] = 0; }
        send(0xB0u | ch | (123u << 8));   // all notes off
        send(0xB0u | ch | (120u << 8));   // all sound off
        send(0xB0u | ch | (64u  << 8));   // sustain off
    }
    LeaveCriticalSection(&g_midi_cs);
}

// -------------------------------------------------------------- keyboard ----
// Scancode output: what the OS sees is indistinguishable from a real key press
// at the SendInput level, which is what DAW shortcuts and most games read.
static void push_key(INPUT* in, int& n, uint16_t sc, bool ext, bool down){
    INPUT& e = in[n++];
    memset(&e, 0, sizeof e);
    e.type = INPUT_KEYBOARD;
    e.ki.wScan = sc;
    e.ki.dwFlags = KEYEVENTF_SCANCODE | (ext ? KEYEVENTF_EXTENDEDKEY : 0) | (down ? 0 : KEYEVENTF_KEYUP);
    // Let Windows derive the virtual key from the scancode.
    e.ki.wVk = 0;
}
void key_send(uint16_t scancode, bool extended, uint32_t mods, bool down){
    if (!scancode) return;
    INPUT in[10]; int n = 0;
    if (down){
        if (mods & KMOD_CTRL)  push_key(in, n, 0x1D, false, true);
        if (mods & KMOD_SHIFT) push_key(in, n, 0x2A, false, true);
        if (mods & KMOD_ALT)   push_key(in, n, 0x38, false, true);
        if (mods & KMOD_WIN)   push_key(in, n, 0x5B, true,  true);
        push_key(in, n, scancode, extended, true);
    } else {
        push_key(in, n, scancode, extended, false);
        if (mods & KMOD_WIN)   push_key(in, n, 0x5B, true,  false);
        if (mods & KMOD_ALT)   push_key(in, n, 0x38, false, false);
        if (mods & KMOD_SHIFT) push_key(in, n, 0x2A, false, false);
        if (mods & KMOD_CTRL)  push_key(in, n, 0x1D, false, false);
    }
    SendInput((UINT)n, in, sizeof(INPUT));
}

// ------------------------------------------------------------- telemetry ----
void Telemetry::reset(){
    reports.store(0); last_interval_us.store(0);
    min_interval_us.store(0xFFFFFFFF); max_interval_us.store(0);
    last_process_us.store(0); max_process_us.store(0);
    hz.store(0.f); jitter_ms.store(0.f);
}

// ------------------------------------------------------------------ misc ----
std::string exe_dir(){
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf);
    size_t p = s.find_last_of("\\/");
    return (p == std::string::npos) ? "." : s.substr(0, p);
}
