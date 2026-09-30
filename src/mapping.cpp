#include "app.h"
#include <cstdio>
#include <cstdlib>
#include <cctype>

// ------------------------------------------------------------ name tables ----
struct CName { const char* shrt; const char* full; };
static const CName g_names[C_COUNT] = {
    {"A","A"},{"B","B"},{"X","X"},{"Y","Y"},
    {"LB","Left bumper"},{"RB","Right bumper"},
    {"BACK","Back / View"},{"START","Start / Menu"},{"GUIDE","Guide / Home"},
    {"LS","Left stick click"},{"RS","Right stick click"},
    {"DU","D-pad up"},{"DD","D-pad down"},{"DL","D-pad left"},{"DR","D-pad right"},
    {"L4","Paddle L4"},{"R4","Paddle R4"},{"L5","Paddle L5"},{"R5","Paddle R5"},
    {"LX","Left stick X"},{"LY","Left stick Y"},{"RX","Right stick X"},{"RY","Right stick Y"},
    {"LT","Left trigger"},{"RT","Right trigger"},
    {"LX-","Left stick left"},{"LX+","Left stick right"},
    {"LY-","Left stick up"},{"LY+","Left stick down"},
    {"RX-","Right stick left"},{"RX+","Right stick right"},
    {"RY-","Right stick up"},{"RY+","Right stick down"},
    {"LT+","Left trigger pulled"},{"RT+","Right trigger pulled"},
};
const char* control_name (int c){ return (c>=0&&c<C_COUNT)? g_names[c].full : "?"; }
const char* control_short(int c){ return (c>=0&&c<C_COUNT)? g_names[c].shrt : "?"; }
bool control_is_axis(int c){ return c >= C_AX_BEGIN && c < C_AX_END; }
bool control_is_dir (int c){ return c >= C_DIR_BEGIN && c < C_COUNT; }

// dir slots: 0 LX- 1 LX+ 2 LY- 3 LY+ 4 RX- 5 RX+ 6 RY- 7 RY+ 8 LT+ 9 RT+
void dir_decode(int ctrl, int* axis, int* sign){
    int d = ctrl - C_DIR_BEGIN;
    if (d >= 8) { *axis = (d == 8) ? AX_LT : AX_RT; *sign = +1; return; }
    *axis = d / 2;                 // 0..3 -> AX_LX..AX_RY
    *sign = (d & 1) ? +1 : -1;
}

static const char* g_kinds[ACT_KIND_COUNT] = {
    "none","note","cc","pitchbend","program","key","transpose","layer","panic"
};
const char* action_kind_name(int k){ return (k>=0&&k<ACT_KIND_COUNT)? g_kinds[k] : "none"; }
static int kind_from_name(const char* s){
    for (int i=0;i<ACT_KIND_COUNT;i++) if (!_stricmp(s,g_kinds[i])) return i;
    return ACT_NONE;
}
const char* backend_name(int b){
    switch(b){ case BE_HID: return "HID direct"; case BE_RAWINPUT: return "Raw Input";
               case BE_XINPUT: return "XInput poll"; }
    return "?";
}

// ---------------------------------------------------------------- profile ----
DeviceProfile* Profile::find_device(const std::string& k){
    for (auto& d : devices) if (d.key == k) return &d;
    return nullptr;
}
DeviceProfile* Profile::ensure_device(const std::string& k, const std::string& label){
    if (auto* d = find_device(k)) return d;
    DeviceProfile d; d.key = k; d.label = label;
    devices.push_back(d);
    return &devices.back();
}

// ------------------------------------------------------------- ini output ----
static void w(FILE* f, const char* k, int v)          { fprintf(f, "%s = %d\n", k, v); }
static void w(FILE* f, const char* k, float v)        { fprintf(f, "%s = %.4f\n", k, v); }
static void w(FILE* f, const char* k, const std::string& v){ fprintf(f, "%s = %s\n", k, v.c_str()); }

bool profile_save(const Profile& p, const std::string& path){
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "# ClaudeController profile - safe to edit by hand\n");
    w(f,"name", p.name);
    w(f,"midi_port", p.midi_port);
    w(f,"transpose", p.transpose);
    w(f,"cc_min_gap_us", p.cc_min_gap_us);
    w(f,"keys_enabled", (int)p.keys_enabled);
    w(f,"midi_enabled", (int)p.midi_enabled);

    for (const auto& d : p.devices){
        fprintf(f, "\n[device %s]\n", d.key.c_str());
        w(f,"label", d.label);
        for (int i=0;i<C_BTN_COUNT;i++)
            if (d.btn[i].kind != HW_NONE)
                fprintf(f,"btn.%s = %d %d\n", g_names[i].shrt, d.btn[i].kind, d.btn[i].index);
        for (int a=0;a<AX_COUNT;a++){
            if (d.axis[a].kind != HW_NONE)
                fprintf(f,"axis.%s = %d %d\n", g_names[C_AX_BEGIN+a].shrt, d.axis[a].kind, d.axis[a].index);
            fprintf(f,"axis.%s.inv = %d\n", g_names[C_AX_BEGIN+a].shrt, (int)d.axis_invert[a]);
            fprintf(f,"axis.%s.dz = %.4f\n", g_names[C_AX_BEGIN+a].shrt, d.axis_deadzone[a]);
        }
    }
    for (int L=0;L<LAYER_COUNT;L++)
    for (int c=0;c<C_COUNT;c++){
        const Action& a = p.layer[L][c];
        if (!a.assigned()) continue;
        fprintf(f, "\n[map %d %s]\n", L, g_names[c].shrt);
        w(f,"kind", std::string(action_kind_name(a.kind)));
        w(f,"channel", a.channel);
        w(f,"number", a.number);
        w(f,"velocity", a.velocity);
        w(f,"off_value", a.off_value);
        w(f,"latch", (int)a.latch);
        w(f,"transpose", a.transpose);
        w(f,"scancode", (int)a.scancode);
        w(f,"extended", (int)a.extended);
        w(f,"mods", (int)a.mods);
        w(f,"key_repeat", (int)a.key_repeat);
        w(f,"deadzone", a.deadzone);
        w(f,"out_min", a.out_min);
        w(f,"out_max", a.out_max);
        w(f,"curve", a.curve);
        w(f,"invert", (int)a.invert);
        w(f,"bipolar", (int)a.bipolar);
        w(f,"threshold", a.threshold);
        w(f,"vel_from", a.vel_from);
    }
    fclose(f);
    return true;
}

static void trim(std::string& s){
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    s = (b == std::string::npos) ? "" : s.substr(b, e-b+1);
}
static int ctrl_from_short(const std::string& s){
    for (int i=0;i<C_COUNT;i++) if (s == g_names[i].shrt) return i;
    return -1;
}

bool profile_load(Profile& p, const std::string& path){
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    p = Profile();
    for (int L=0;L<LAYER_COUNT;L++) for (int c=0;c<C_COUNT;c++) p.layer[L][c] = Action();

    enum { S_ROOT, S_DEV, S_MAP } sec = S_ROOT;
    DeviceProfile* dev = nullptr;
    Action*        act = nullptr;
    char line[1024];
    while (fgets(line, sizeof line, f)){
        std::string s(line); trim(s);
        if (s.empty() || s[0]=='#' || s[0]==';') continue;
        if (s[0]=='['){
            size_t close = s.find(']');
            if (close == std::string::npos) continue;
            std::string head = s.substr(1, close-1);
            if (head.rfind("device ",0)==0){
                std::string key = head.substr(7); trim(key);
                dev = p.ensure_device(key, key);
                sec = S_DEV;
            } else if (head.rfind("map ",0)==0){
                int L = 0; char tok[64] = {0};
                if (sscanf(head.c_str()+4, "%d %63s", &L, tok) == 2){
                    int c = ctrl_from_short(tok);
                    if (c >= 0 && L >= 0 && L < LAYER_COUNT){ act = &p.layer[L][c]; sec = S_MAP; }
                    else { act = nullptr; sec = S_ROOT; }
                }
            } else sec = S_ROOT;
            continue;
        }
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = s.substr(0,eq), v = s.substr(eq+1);
        trim(k); trim(v);
        double num = atof(v.c_str());
        int    iv  = atoi(v.c_str());

        if (sec == S_ROOT){
            if (k=="name") p.name = v;
            else if (k=="midi_port") p.midi_port = v;
            else if (k=="transpose") p.transpose = iv;
            else if (k=="cc_min_gap_us") p.cc_min_gap_us = iv;
            else if (k=="keys_enabled") p.keys_enabled = iv != 0;
            else if (k=="midi_enabled") p.midi_enabled = iv != 0;
        } else if (sec == S_DEV && dev){
            if (k=="label") dev->label = v;
            else if (k.rfind("btn.",0)==0){
                int c = ctrl_from_short(k.substr(4));
                int kk=0, ix=-1;
                if (c >= 0 && c < C_BTN_COUNT && sscanf(v.c_str(), "%d %d", &kk, &ix)==2){
                    dev->btn[c].kind = kk; dev->btn[c].index = ix;
                }
            } else if (k.rfind("axis.",0)==0){
                std::string rest = k.substr(5);
                std::string name = rest; std::string sub;
                size_t dot = rest.find('.');
                if (dot != std::string::npos){ name = rest.substr(0,dot); sub = rest.substr(dot+1); }
                int c = ctrl_from_short(name);
                if (c >= C_AX_BEGIN && c < C_AX_END){
                    int a = c - C_AX_BEGIN;
                    if (sub.empty()){
                        int kk=0, ix=-1;
                        if (sscanf(v.c_str(), "%d %d", &kk, &ix)==2){ dev->axis[a].kind=kk; dev->axis[a].index=ix; }
                    } else if (sub=="inv") dev->axis_invert[a] = iv != 0;
                    else if (sub=="dz")    dev->axis_deadzone[a] = (float)num;
                }
            }
        } else if (sec == S_MAP && act){
            if (k=="kind") act->kind = kind_from_name(v.c_str());
            else if (k=="channel")   act->channel = iv;
            else if (k=="number")    act->number = iv;
            else if (k=="velocity")  act->velocity = iv;
            else if (k=="off_value") act->off_value = iv;
            else if (k=="latch")     act->latch = iv != 0;
            else if (k=="transpose") act->transpose = iv;
            else if (k=="scancode")  act->scancode = (uint16_t)iv;
            else if (k=="extended")  act->extended = iv != 0;
            else if (k=="mods")      act->mods = (uint32_t)iv;
            else if (k=="key_repeat")act->key_repeat = iv != 0;
            else if (k=="deadzone")  act->deadzone = (float)num;
            else if (k=="out_min")   act->out_min = (float)num;
            else if (k=="out_max")   act->out_max = (float)num;
            else if (k=="curve")     act->curve = (float)num;
            else if (k=="invert")    act->invert = iv != 0;
            else if (k=="bipolar")   act->bipolar = iv != 0;
            else if (k=="threshold") act->threshold = (float)num;
            else if (k=="vel_from")  act->vel_from = iv;
        }
    }
    fclose(f);
    return true;
}

// ------------------------------------------------------------- templates ----
static Action note_a(int ch,int n,int vel){ Action a; a.kind=ACT_NOTE; a.channel=ch; a.number=n; a.velocity=vel; return a; }
static Action cc_a  (int ch,int n,bool bip){ Action a; a.kind=ACT_CC; a.channel=ch; a.number=n; a.bipolar=bip; a.velocity=127; a.off_value=0; return a; }
static Action key_a (uint16_t sc,bool ext){ Action a; a.kind=ACT_KEY; a.scancode=sc; a.extended=ext; return a; }
static Action tr_a  (int semi){ Action a; a.kind=ACT_TRANSPOSE; a.transpose=semi; return a; }

// set 1 scancodes
enum { SC_ESC=0x01, SC_TAB=0x0F, SC_Q=0x10, SC_W=0x11, SC_E=0x12, SC_ENTER=0x1C,
       SC_LCTRL=0x1D, SC_A=0x1E, SC_S=0x1F, SC_D=0x20, SC_LSHIFT=0x2A,
       SC_Z=0x2C, SC_X=0x2D, SC_C=0x2E, SC_V=0x2F, SC_SPACE=0x39,
       SC_UP=0x48, SC_LEFT=0x4B, SC_RIGHT=0x4D, SC_DOWN=0x50 };

const char* template_name(int w){
    switch(w){ case 0: return "Blank";
               case 1: return "Drum pads";
               case 2: return "Chromatic notes";
               case 3: return "Keyboard keys"; }
    return "?";
}
const char* template_blurb(int w){
    switch(w){
    case 0: return "Nothing mapped. Build it from scratch.";
    case 1: return "Face buttons and d-pad fire GM drum notes on channel 10. Triggers send mod and expression.";
    case 2: return "Ten notes laid out across the pad from C3, bumpers shift the octave, right stick bends.";
    case 3: return "Arrows on the d-pad, WASD on the left stick, space and modifiers on the face buttons.";
    }
    return "";
}

void profile_template(Profile& p, int which){
    for (int L=0;L<LAYER_COUNT;L++) for (int c=0;c<C_COUNT;c++) p.layer[L][c] = Action();
    p.name = template_name(which);
    if (which == 1){
        const int ch = 10;
        p.layer[0][C_A]     = note_a(ch,36,110); // kick
        p.layer[0][C_B]     = note_a(ch,38,110); // snare
        p.layer[0][C_X]     = note_a(ch,42,100); // closed hat
        p.layer[0][C_Y]     = note_a(ch,46,100); // open hat
        p.layer[0][C_LB]    = note_a(ch,49,110); // crash
        p.layer[0][C_RB]    = note_a(ch,51,100); // ride
        p.layer[0][C_DU]    = note_a(ch,48,105); // hi tom
        p.layer[0][C_DL]    = note_a(ch,45,105); // low tom
        p.layer[0][C_DR]    = note_a(ch,47,105); // mid tom
        p.layer[0][C_DD]    = note_a(ch,41,105); // floor tom
        p.layer[0][C_LS]    = note_a(ch,39,110); // clap
        p.layer[0][C_RS]    = note_a(ch,37,100); // rim
        p.layer[0][C_AX_LT] = cc_a(1,1,false);   // mod
        p.layer[0][C_AX_RT] = cc_a(1,11,false);  // expression
        p.layer[0][C_AX_LX] = cc_a(1,16,true);
        p.layer[0][C_AX_LY] = cc_a(1,17,true);
        p.layer[0][C_AX_RX] = cc_a(1,18,true);
        p.layer[0][C_AX_RY] = cc_a(1,19,true);
        p.layer[0][C_START].kind = ACT_PANIC;
        p.layer[0][C_L4]    = tr_a(-12);
        p.layer[0][C_R4]    = tr_a(+12);
    } else if (which == 2){
        const int base = 60; // C4
        const int seq[10] = { C_DL, C_DD, C_DR, C_DU, C_X, C_A, C_B, C_Y, C_LS, C_RS };
        const int semi[10]= { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16 };
        for (int i=0;i<10;i++) p.layer[0][seq[i]] = note_a(1, base+semi[i], 100);
        p.layer[0][C_LB]    = tr_a(-12);
        p.layer[0][C_RB]    = tr_a(+12);
        p.layer[0][C_AX_LT] = cc_a(1,1,false);
        p.layer[0][C_AX_RT] = cc_a(1,11,false);
        { Action a; a.kind=ACT_PITCHBEND; a.channel=1; a.bipolar=true; p.layer[0][C_AX_RX]=a; }
        p.layer[0][C_AX_RY] = cc_a(1,74,true);
        p.layer[0][C_AX_LY] = cc_a(1,2,true);
        p.layer[0][C_START].kind = ACT_PANIC;
        p.layer[0][C_L4].kind    = ACT_LAYER;
    } else if (which == 3){
        p.layer[0][C_DU] = key_a(SC_UP,true);
        p.layer[0][C_DD] = key_a(SC_DOWN,true);
        p.layer[0][C_DL] = key_a(SC_LEFT,true);
        p.layer[0][C_DR] = key_a(SC_RIGHT,true);
        p.layer[0][C_A]  = key_a(SC_SPACE,false);
        p.layer[0][C_B]  = key_a(SC_ENTER,false);
        p.layer[0][C_X]  = key_a(SC_Z,false);
        p.layer[0][C_Y]  = key_a(SC_X,false);
        p.layer[0][C_LB] = key_a(SC_LSHIFT,false);
        p.layer[0][C_RB] = key_a(SC_LCTRL,false);
        p.layer[0][C_BACK]  = key_a(SC_ESC,false);
        p.layer[0][C_START] = key_a(SC_TAB,false);
        p.layer[0][C_DIR_BEGIN+2] = key_a(SC_W,false);  // LY-
        p.layer[0][C_DIR_BEGIN+3] = key_a(SC_S,false);  // LY+
        p.layer[0][C_DIR_BEGIN+0] = key_a(SC_A,false);  // LX-
        p.layer[0][C_DIR_BEGIN+1] = key_a(SC_D,false);  // LX+
        p.layer[0][C_DIR_BEGIN+8] = key_a(SC_Q,false);  // LT+
        p.layer[0][C_DIR_BEGIN+9] = key_a(SC_E,false);  // RT+
        p.layer[0][C_L4].kind = ACT_LAYER;
    }
}
