#include "app.h"
#include "imgui.h"
#include <commdlg.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

void device_autobind(Device* dev, DeviceProfile* dp);

// ------------------------------------------------------------------ theme ----
namespace T {
    static const ImU32 bg      = IM_COL32(0x0E,0x11,0x16,255);
    static const ImU32 panel   = IM_COL32(0x16,0x1B,0x22,255);
    static const ImU32 panel2  = IM_COL32(0x1C,0x23,0x2B,255);
    static const ImU32 line    = IM_COL32(0x28,0x31,0x3C,255);
    static const ImU32 text    = IM_COL32(0xE6,0xED,0xF3,255);
    static const ImU32 dim     = IM_COL32(0x8B,0x98,0xA5,255);
    static const ImU32 dimmer  = IM_COL32(0x5C,0x68,0x74,255);
    static const ImU32 accent  = IM_COL32(0x4C,0xC2,0xFF,255);
    static const ImU32 accent2 = IM_COL32(0xFF,0xB8,0x4C,255);
    static const ImU32 ok      = IM_COL32(0x3F,0xD6,0x8C,255);
    static const ImU32 warn    = IM_COL32(0xFF,0x6B,0x6B,255);
    static ImVec4 v(ImU32 c){ return ImGui::ColorConvertU32ToFloat4(c); }
}

static void apply_style(){
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.f;
    s.ChildRounding = 10.f;
    s.FrameRounding = 7.f;
    s.PopupRounding = 10.f;
    s.GrabRounding = 7.f;
    s.TabRounding = 8.f;
    s.ScrollbarRounding = 8.f;
    s.WindowPadding = ImVec2(14,14);
    s.FramePadding = ImVec2(10,6);
    s.ItemSpacing = ImVec2(9,8);
    s.ItemInnerSpacing = ImVec2(7,5);
    s.WindowBorderSize = 0.f;
    s.ChildBorderSize = 1.f;
    s.FrameBorderSize = 0.f;
    s.ScrollbarSize = 11.f;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]            = T::v(T::bg);
    c[ImGuiCol_ChildBg]             = T::v(T::panel);
    c[ImGuiCol_PopupBg]             = T::v(T::panel2);
    c[ImGuiCol_Border]              = T::v(T::line);
    c[ImGuiCol_Text]                = T::v(T::text);
    c[ImGuiCol_TextDisabled]        = T::v(T::dimmer);
    c[ImGuiCol_FrameBg]             = T::v(IM_COL32(0x22,0x2A,0x33,255));
    c[ImGuiCol_FrameBgHovered]      = T::v(IM_COL32(0x2A,0x34,0x3F,255));
    c[ImGuiCol_FrameBgActive]       = T::v(IM_COL32(0x30,0x3C,0x48,255));
    c[ImGuiCol_Button]              = T::v(IM_COL32(0x25,0x2E,0x38,255));
    c[ImGuiCol_ButtonHovered]       = T::v(IM_COL32(0x32,0x3E,0x4A,255));
    c[ImGuiCol_ButtonActive]        = T::v(IM_COL32(0x3C,0x4A,0x58,255));
    c[ImGuiCol_Header]              = T::v(IM_COL32(0x25,0x2E,0x38,255));
    c[ImGuiCol_HeaderHovered]       = T::v(IM_COL32(0x32,0x3E,0x4A,255));
    c[ImGuiCol_HeaderActive]        = T::v(IM_COL32(0x3C,0x4A,0x58,255));
    c[ImGuiCol_CheckMark]           = T::v(T::accent);
    c[ImGuiCol_SliderGrab]          = T::v(T::accent);
    c[ImGuiCol_SliderGrabActive]    = T::v(T::accent);
    c[ImGuiCol_Separator]           = T::v(T::line);
    c[ImGuiCol_Tab]                 = T::v(IM_COL32(0x1C,0x23,0x2B,255));
    c[ImGuiCol_TabHovered]          = T::v(IM_COL32(0x2C,0x38,0x44,255));
    c[ImGuiCol_TabSelected]         = T::v(IM_COL32(0x33,0x42,0x51,255));
    c[ImGuiCol_TitleBg]             = T::v(T::panel);
    c[ImGuiCol_TitleBgActive]       = T::v(T::panel);
    c[ImGuiCol_ScrollbarBg]         = T::v(IM_COL32(0,0,0,0));
    c[ImGuiCol_ScrollbarGrab]       = T::v(IM_COL32(0x33,0x3E,0x4A,255));
    c[ImGuiCol_TableHeaderBg]       = T::v(T::panel2);
    c[ImGuiCol_TableBorderLight]    = T::v(T::line);
    c[ImGuiCol_TableBorderStrong]   = T::v(T::line);
}

// ------------------------------------------------------------------ state ----
struct Gui {
    Profile  prof;
    std::vector<std::shared_ptr<Device>> devs;
    int   sel = C_A;
    int   layer_view = 0;
    bool  dirty = false;
    bool  quit = false;
    char  name_buf[64] = "Default";
    std::string profile_path;
    std::vector<MidiPortInfo> ports;
    int   port_sel = -1;
    int   bottom_tab = 0;

    // first-run wizard
    int   intro_step = 0;            // 0 device, 1 purpose, 2 template, 3 done
    bool  intro_open = true;
    std::vector<char> intro_pick;
    int   intro_purpose = 0;         // 0 midi 1 keys 2 both
    int   intro_template = 1;

    // hardware setup wizard
    int   hw_dev = -1;
    int   hw_step = 0;
    bool  hw_open = false;
    RawSample hw_base;
    bool  hw_base_ready = false;

    // key capture
    bool  key_capture = false;
    uint16_t cap_sc = 0; bool cap_ext = false; bool cap_got = false;

    bool  show_labels = true;
};
static Gui G;

// --------------------------------------------------------------- utilities ----
static const char* note_name(int n, char* buf, int len){
    static const char* nm[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    if (n < 0) n = 0;
    if (n > 127) n = 127;
    snprintf(buf, len, "%s%d", nm[n % 12], n / 12 - 1);
    return buf;
}
static std::string key_label(uint16_t sc, bool ext, uint32_t mods){
    if (!sc) return "unassigned";
    char name[64] = {};
    LONG lp = ((LONG)sc << 16) | (ext ? (1L << 24) : 0);
    if (GetKeyNameTextA(lp, name, sizeof name) <= 0) snprintf(name, sizeof name, "sc 0x%02X", sc);
    std::string s;
    if (mods & KMOD_CTRL)  s += "Ctrl+";
    if (mods & KMOD_SHIFT) s += "Shift+";
    if (mods & KMOD_ALT)   s += "Alt+";
    if (mods & KMOD_WIN)   s += "Win+";
    return s + name;
}
// One-line summary of an action, used on the pad graphic and in the table.
static std::string action_summary(const Action& a){
    char buf[96], nb[16];
    switch (a.kind){
    case ACT_NONE:      return "";
    case ACT_NOTE:      snprintf(buf,sizeof buf,"%s ch%d%s", note_name(a.number,nb,sizeof nb), a.channel, a.latch?" latch":""); return buf;
    case ACT_CC:        snprintf(buf,sizeof buf,"CC%d ch%d", a.number, a.channel); return buf;
    case ACT_PITCHBEND: snprintf(buf,sizeof buf,"bend ch%d", a.channel); return buf;
    case ACT_PROGRAM:   snprintf(buf,sizeof buf,"prog %d", a.number); return buf;
    case ACT_KEY:       return key_label(a.scancode, a.extended, a.mods);
    case ACT_TRANSPOSE: snprintf(buf,sizeof buf,"%+d semi", a.transpose); return buf;
    case ACT_LAYER:     return "hold layer 2";
    case ACT_PANIC:     return "panic";
    }
    return "";
}
static bool is_down(int ctrl){
    for (auto& d : G.devs){
        if (!d->enabled) continue;
        if (ctrl < C_BTN_COUNT){ if (d->last_norm.buttons & (1u << ctrl)) return true; }
        else if (control_is_dir(ctrl)){
            int ax, sg; dir_decode(ctrl, &ax, &sg);
            if (d->last_norm.axis[ax] * sg > 0.5f) return true;
        } else {
            int ax = ctrl - C_AX_BEGIN;
            float v = d->last_norm.axis[ax];
            if ((v < 0 ? -v : v) > 0.15f) return true;
        }
    }
    return false;
}
static float axis_now(int ax){
    for (auto& d : G.devs) if (d->enabled) {
        float v = d->last_norm.axis[ax];
        if ((v < 0 ? -v : v) > 0.02f) return v;
    }
    return 0.f;
}
static ImU32 mix(ImU32 a, ImU32 b, float t){
    ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x+(y.x-x.x)*t, x.y+(y.y-x.y)*t, x.z+(y.z-x.z)*t, 1.f));
}
static void chip(const char* txt, ImU32 col, ImU32 fg = T::text){
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz = ImGui::CalcTextSize(txt);
    ImVec2 pad(9,4);
    ImVec2 e(p.x + sz.x + pad.x*2, p.y + sz.y + pad.y*2);
    dl->AddRectFilled(p, e, col, 6.f);
    dl->AddText(ImVec2(p.x+pad.x, p.y+pad.y), fg, txt);
    ImGui::Dummy(ImVec2(sz.x + pad.x*2, sz.y + pad.y*2));
}
static void section(const char* label){
    ImGui::Dummy(ImVec2(0,2));
    ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dim));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0,1));
}

// ----------------------------------------------------------- pad rendering ----
// Centre-based layout in 0..1 of the pad panel. Round items take their height
// from their width so circles stay circles at any panel aspect.
struct PadItem { int ctrl; float cx, cy, w, h; bool round; const char* face; };
static const PadItem g_pad[] = {
    { C_AX_LT, 0.205f, 0.055f, 0.110f, 0.058f, false, "LT" },
    { C_AX_RT, 0.795f, 0.055f, 0.110f, 0.058f, false, "RT" },
    { C_LB,    0.205f, 0.145f, 0.110f, 0.052f, false, "LB" },
    { C_RB,    0.795f, 0.145f, 0.110f, 0.052f, false, "RB" },

    { C_LS,    0.235f, 0.400f, 0.125f, 0.f,    true,  "LS" },
    { C_RS,    0.630f, 0.700f, 0.125f, 0.f,    true,  "RS" },

    { C_Y,     0.805f, 0.300f, 0.060f, 0.f,    true,  "Y" },
    { C_X,     0.730f, 0.435f, 0.060f, 0.f,    true,  "X" },
    { C_B,     0.880f, 0.435f, 0.060f, 0.f,    true,  "B" },
    { C_A,     0.805f, 0.570f, 0.060f, 0.f,    true,  "A" },

    { C_DU,    0.385f, 0.630f, 0.058f, 0.048f, false, "" },
    { C_DL,    0.310f, 0.722f, 0.058f, 0.048f, false, "" },
    { C_DR,    0.460f, 0.722f, 0.058f, 0.048f, false, "" },
    { C_DD,    0.385f, 0.815f, 0.058f, 0.048f, false, "" },

    { C_BACK,  0.440f, 0.330f, 0.058f, 0.042f, false, "Bk" },
    { C_GUIDE, 0.515f, 0.250f, 0.058f, 0.f,    true,  "H" },
    { C_START, 0.590f, 0.330f, 0.058f, 0.042f, false, "St" },

    { C_L4,    0.120f, 0.900f, 0.098f, 0.048f, false, "L4" },
    { C_L5,    0.245f, 0.900f, 0.098f, 0.048f, false, "L5" },
    { C_R5,    0.755f, 0.900f, 0.098f, 0.048f, false, "R5" },
    { C_R4,    0.880f, 0.900f, 0.098f, 0.048f, false, "R4" },
};
static const int g_pad_n = (int)(sizeof g_pad / sizeof g_pad[0]);

// Short caption: what this control sends, in as few characters as it takes.
static std::string caption(const Action& a){
    char buf[32], nb[16];
    switch (a.kind){
    case ACT_NONE:      return "";
    case ACT_NOTE:      return note_name(a.number, nb, sizeof nb);
    case ACT_CC:        snprintf(buf,sizeof buf,"CC%d", a.number); return buf;
    case ACT_PITCHBEND: return "bend";
    case ACT_PROGRAM:   snprintf(buf,sizeof buf,"pg%d", a.number); return buf;
    case ACT_KEY:       return key_label(a.scancode, a.extended, a.mods);
    case ACT_TRANSPOSE: snprintf(buf,sizeof buf,"%+d", a.transpose); return buf;
    case ACT_LAYER:     return "layer";
    case ACT_PANIC:     return "panic";
    }
    return "";
}

static void draw_pad(ImVec2 origin, ImVec2 size, bool show_labels){
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b0(origin.x + size.x*0.040f, origin.y + size.y*0.115f);
    ImVec2 b1(origin.x + size.x*0.960f, origin.y + size.y*0.965f);
    float rnd = size.y*0.15f;
    dl->AddRectFilled(b0, b1, IM_COL32(0x12,0x17,0x1D,255), rnd);
    dl->AddRect(b0, b1, IM_COL32(0x23,0x2B,0x35,255), rnd, 0, 1.5f);

    const Action* lay = G.prof.layer[G.layer_view];

    for (int i = 0; i < g_pad_n; i++){
        const PadItem& it = g_pad[i];
        float w = it.w * size.x;
        float h = it.round ? w : it.h * size.y;
        ImVec2 c(origin.x + it.cx*size.x, origin.y + it.cy*size.y);
        ImVec2 p(c.x - w*0.5f, c.y - h*0.5f);
        ImVec2 q(c.x + w*0.5f, c.y + h*0.5f);

        ImGui::SetCursorScreenPos(p);
        char id[32]; snprintf(id, sizeof id, "##pad%d", it.ctrl);
        ImGui::InvisibleButton(id, ImVec2(w, h));
        bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) G.sel = it.ctrl;

        bool sel    = (G.sel == it.ctrl);
        bool down   = is_down(it.ctrl);
        bool mapped = lay[it.ctrl].assigned();

        ImU32 fill = mapped ? IM_COL32(0x26,0x31,0x3C,255) : IM_COL32(0x1A,0x20,0x27,255);
        if (hov)  fill = mix(fill, T::accent, 0.20f);
        if (down) fill = T::accent2;
        ImU32 edge = sel ? T::accent : (mapped ? IM_COL32(0x3A,0x48,0x56,255) : IM_COL32(0x23,0x2B,0x34,255));
        ImU32 fg   = down ? IM_COL32(0x12,0x14,0x18,255) : (mapped ? T::text : T::dimmer);

        if (it.round){
            dl->AddCircleFilled(c, w*0.5f, fill, 40);
            dl->AddCircle(c, w*0.5f, edge, 40, sel ? 2.4f : 1.4f);
        } else {
            dl->AddRectFilled(p, q, fill, h*0.34f);
            dl->AddRect(p, q, edge, h*0.34f, 0, sel ? 2.4f : 1.4f);
        }

        if (it.ctrl == C_AX_LT || it.ctrl == C_AX_RT){
            float v = axis_now(it.ctrl == C_AX_LT ? AX_LT : AX_RT);
            if (v > 0.01f)
                dl->AddRectFilled(ImVec2(p.x, q.y - h*v), q, IM_COL32(0x4C,0xC2,0xFF,150), h*0.34f);
        }
        if (it.ctrl == C_LS || it.ctrl == C_RS){
            float ax = axis_now(it.ctrl == C_LS ? AX_LX : AX_RX);
            float ay = axis_now(it.ctrl == C_LS ? AX_LY : AX_RY);
            float r = w*0.5f;
            dl->AddCircle(c, r*0.66f, IM_COL32(0x2C,0x37,0x43,255), 28, 1.f);
            bool moved = (fabsf(ax) + fabsf(ay)) > 0.06f;
            dl->AddCircleFilled(ImVec2(c.x + ax*r*0.5f, c.y + ay*r*0.5f), r*0.26f,
                                moved ? T::accent : IM_COL32(0x4A,0x57,0x64,255), 20);
        } else if (it.ctrl >= C_DU && it.ctrl <= C_DR){
            float r = h*0.26f;
            ImVec2 t1, t2, t3;
            switch (it.ctrl){
            case C_DU: t1=ImVec2(c.x,c.y-r); t2=ImVec2(c.x-r,c.y+r*0.8f); t3=ImVec2(c.x+r,c.y+r*0.8f); break;
            case C_DD: t1=ImVec2(c.x,c.y+r); t2=ImVec2(c.x-r,c.y-r*0.8f); t3=ImVec2(c.x+r,c.y-r*0.8f); break;
            case C_DL: t1=ImVec2(c.x-r,c.y); t2=ImVec2(c.x+r*0.8f,c.y-r); t3=ImVec2(c.x+r*0.8f,c.y+r); break;
            default:   t1=ImVec2(c.x+r,c.y); t2=ImVec2(c.x-r*0.8f,c.y-r); t3=ImVec2(c.x-r*0.8f,c.y+r); break;
            }
            dl->AddTriangleFilled(t1, t2, t3, fg);
        } else if (it.face[0]){
            ImVec2 ts = ImGui::CalcTextSize(it.face);
            dl->AddText(ImVec2(c.x - ts.x*0.5f, c.y - ts.y*0.5f), fg, it.face);
        }

        if (show_labels && mapped && it.ctrl != C_LS && it.ctrl != C_RS){
            std::string cap = caption(lay[it.ctrl]);
            if (!cap.empty()){
                ImVec2 ts = ImGui::CalcTextSize(cap.c_str());
                dl->AddText(ImVec2(c.x - ts.x*0.5f, q.y + 2.f), down ? T::accent2 : T::dim, cap.c_str());
            }
        }
        if (hov){
            std::string full = mapped ? action_summary(lay[it.ctrl]) : std::string("not assigned - click to map");
            ImGui::SetTooltip("%s\n%s", control_name(it.ctrl), full.c_str());
        }
    }
}

// ------------------------------------------------------------ chips for axes --
static void axis_chips(){
    const int list[] = { C_AX_LX, C_AX_LY, C_AX_RX, C_AX_RY, C_AX_LT, C_AX_RT,
                         C_DIR_BEGIN+2, C_DIR_BEGIN+3, C_DIR_BEGIN+0, C_DIR_BEGIN+1,
                         C_DIR_BEGIN+6, C_DIR_BEGIN+7, C_DIR_BEGIN+4, C_DIR_BEGIN+5,
                         C_DIR_BEGIN+8, C_DIR_BEGIN+9 };
    const Action* lay = G.prof.layer[G.layer_view];
    float avail = ImGui::GetContentRegionAvail().x;
    float used = 0;
    for (int k = 0; k < (int)(sizeof list/sizeof list[0]); k++){
        int c = list[k];
        char lbl[64];
        std::string sum = action_summary(lay[c]);
        if (sum.empty()) snprintf(lbl, sizeof lbl, "%s", control_short(c));
        else             snprintf(lbl, sizeof lbl, "%s  %s", control_short(c), sum.c_str());
        ImVec2 sz = ImGui::CalcTextSize(lbl);
        float w = sz.x + 20.f;
        if (used > 0 && used + w > avail){ used = 0; } else if (used > 0) ImGui::SameLine();
        used += w + 6.f;

        bool down = is_down(c);
        bool mapped = lay[c].assigned();
        bool sel = (G.sel == c);
        ImU32 bgc = down ? T::accent2 : (mapped ? IM_COL32(0x27,0x33,0x3E,255) : IM_COL32(0x1A,0x20,0x27,255));
        ImGui::PushStyleColor(ImGuiCol_Button, T::v(bgc));
        ImGui::PushStyleColor(ImGuiCol_Text, T::v(down ? IM_COL32(0x12,0x14,0x18,255) : (mapped ? T::text : T::dimmer)));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, sel ? 2.f : 0.f);
        ImGui::PushStyleColor(ImGuiCol_Border, T::v(T::accent));
        if (ImGui::Button(lbl)) G.sel = c;
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", control_name(c));
    }
}

// ------------------------------------------------------------------ actions ---
static void commit(){ g_engine.publish(G.prof); G.dirty = true; }

static void start_device(const std::shared_ptr<Device>& d){
    if (d->enabled) return;
    DeviceProfile* dp = G.prof.find_device(d->key);
    if (!dp){ dp = G.prof.ensure_device(d->key, d->label); device_autobind(d.get(), dp); }
    commit();
    if (!d->start(&g_engine)) g_engine.log("%s: %s", d->label.c_str(), d->error.empty()?"could not start":d->error.c_str());
    else g_engine.log("%s connected via %s", d->label.c_str(), backend_name(d->backend));
}
static void stop_device(const std::shared_ptr<Device>& d){
    if (!d->enabled) return;
    d->stop();
    g_engine.log("%s released", d->label.c_str());
}
static void rescan(){
    for (auto& d : G.devs) stop_device(d);
    G.devs = enumerate_devices();
    G.intro_pick.assign(G.devs.size(), 0);
    g_engine.log("found %d controller interface(s)", (int)G.devs.size());
}
static void refresh_ports(){ G.ports = midi_list_outputs(); }

static bool file_dialog(bool save, std::string& path){
    char buf[MAX_PATH] = {};
    if (!path.empty()) snprintf(buf, sizeof buf, "%s", path.c_str());
    OPENFILENAMEA o = {};
    o.lStructSize = sizeof o;
    o.lpstrFilter = "ClaudeController profile\0*.profile\0All files\0*.*\0";
    o.lpstrFile = buf;
    o.nMaxFile = MAX_PATH;
    o.lpstrDefExt = "profile";
    o.Flags = OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    BOOL ok = save ? GetSaveFileNameA(&o) : GetOpenFileNameA(&o);
    if (!ok) return false;
    path = buf;
    return true;
}
static std::string default_profile_path(){ return exe_dir() + "\\ClaudeController.profile"; }

static void do_save(bool as){
    std::string p = G.profile_path.empty() ? default_profile_path() : G.profile_path;
    if (as && !file_dialog(true, p)) return;
    G.prof.name = G.name_buf;
    if (profile_save(G.prof, p)){ G.profile_path = p; G.dirty = false; g_engine.log("saved %s", p.c_str()); }
    else g_engine.log("could not write %s", p.c_str());
}
static void do_load(){
    std::string p = G.profile_path.empty() ? default_profile_path() : G.profile_path;
    if (!file_dialog(false, p)) return;
    if (profile_load(G.prof, p)){
        G.profile_path = p;
        snprintf(G.name_buf, sizeof G.name_buf, "%s", G.prof.name.c_str());
        commit(); G.dirty = false;
        g_engine.log("loaded %s", p.c_str());
        for (auto& pt : G.ports)
            if (!G.prof.midi_port.empty() && pt.name.find(G.prof.midi_port) != std::string::npos){
                G.port_sel = pt.index; g_engine.set_midi_port(pt.index); break;
            }
    }
}

// ------------------------------------------------------------------ top bar ---
static void top_bar(float h){
    ImGui::BeginChild("top", ImVec2(0, h), ImGuiChildFlags_Borders);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddCircleFilled(ImVec2(p.x+7, p.y+13), 6.f, g_engine.active() ? T::ok : T::dimmer, 16);
    ImGui::Dummy(ImVec2(20,0)); ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("ClaudeController");

    ImGui::SameLine(0, 18);
    ImGui::SetNextItemWidth(160);
    if (ImGui::InputText("##pname", G.name_buf, sizeof G.name_buf)) G.dirty = true;
    ImGui::SameLine(0, 6);
    if (ImGui::Button("Save")) do_save(false);
    ImGui::SameLine(0, 6);
    if (ImGui::Button("Save as")) do_save(true);
    ImGui::SameLine(0, 6);
    if (ImGui::Button("Open")) do_load();

    ImGui::SameLine(0, 18);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dim));
    ImGui::TextUnformatted("MIDI out");
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 6);
    ImGui::SetNextItemWidth(200);
    const char* cur = "none";
    for (auto& pt : G.ports) if (pt.index == G.port_sel) cur = pt.name.c_str();
    if (ImGui::BeginCombo("##port", cur)){
        if (ImGui::Selectable("none", G.port_sel < 0)){ G.port_sel = -1; g_engine.set_midi_port(-1); }
        for (auto& pt : G.ports)
            if (ImGui::Selectable(pt.name.c_str(), pt.index == G.port_sel)){
                G.port_sel = pt.index;
                G.prof.midi_port = pt.name;
                g_engine.set_midi_port(pt.index);
                commit();
            }
        ImGui::Separator();
        if (ImGui::Selectable("rescan ports")) refresh_ports();
        ImGui::EndCombo();
    }

    // Right-hand block, laid out from the edge back. The live numbers only
    // appear when there is honest room for them.
    float best_hz = 0.f, jit = 0.f;
    for (auto& d : G.devs) if (d->enabled && d->tel.hz.load() > best_hz){ best_hz = d->tel.hz.load(); jit = d->tel.jitter_ms.load(); }
    char stat[96];
    snprintf(stat, sizeof stat, "%.0f Hz   jitter %.2f ms   dispatch %u us",
             best_hz, jit, g_engine.worst_us.load());
    const float run_w = 92.f, panic_w = 86.f, gap = 8.f;
    float stat_w = ImGui::CalcTextSize(stat).x;
    float right_w = run_w + panic_w + gap*2;
    float after = ImGui::GetCursorPosX() + 16.f;
    float win_w = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
    bool  room  = (win_w - right_w - stat_w - gap) > after;

    if (room){
        ImGui::SameLine(win_w - right_w - stat_w - gap);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dim));
        ImGui::TextUnformatted(stat);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, gap);
    } else {
        ImGui::SameLine(win_w - right_w > after ? win_w - right_w : after);
    }
    bool on = g_engine.active();
    ImGui::PushStyleColor(ImGuiCol_Button, T::v(on ? IM_COL32(0x1E,0x4D,0x3A,255) : IM_COL32(0x4A,0x2B,0x2B,255)));
    if (ImGui::Button(on ? "Running" : "Paused", ImVec2(run_w, 0))) g_engine.set_active(!on);
    ImGui::PopStyleColor();
    ImGui::SameLine(0, gap);
    ImGui::PushStyleColor(ImGuiCol_Button, T::v(IM_COL32(0x54,0x2A,0x2A,255)));
    if (ImGui::Button("Panic", ImVec2(panic_w, 0))) g_engine.panic();
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

// ----------------------------------------------------------- devices panel ----
static void devices_panel(float w){
    ImGui::BeginChild("devs", ImVec2(w, 0), ImGuiChildFlags_Borders);
    section("CONTROLLERS");
    for (size_t i = 0; i < G.devs.size(); i++){
        auto& d = G.devs[i];
        ImGui::PushID((int)i);
        ImGui::BeginChild("card", ImVec2(0, d->enabled ? 132.f : 96.f), ImGuiChildFlags_Borders);
        ImGui::PushStyleColor(ImGuiCol_Text, T::v(d->enabled ? T::text : T::dim));
        ImGui::TextWrapped("%s", d->label.c_str());
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dimmer));
        ImGui::TextWrapped("%s", d->detail.c_str());
        ImGui::PopStyleColor();

        if (d->enabled){
            float hz = d->tel.hz.load();
            uint32_t mn = d->tel.min_interval_us.load(), mx = d->tel.max_interval_us.load();
            char c1[48]; snprintf(c1, sizeof c1, "%.0f Hz", hz);
            chip(c1, hz > 400.f ? IM_COL32(0x1E,0x4D,0x3A,255) : IM_COL32(0x3C,0x38,0x22,255),
                 hz > 400.f ? T::ok : T::accent2);
            ImGui::SameLine();
            char c2[64]; snprintf(c2, sizeof c2, "%.2f-%.2f ms", mn == 0xFFFFFFFF ? 0.f : mn/1000.f, mx/1000.f);
            chip(c2, IM_COL32(0x22,0x2A,0x33,255), T::dim);
        }
        if (!d->error.empty()){
            ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::warn));
            ImGui::TextWrapped("%s", d->error.c_str());
            ImGui::PopStyleColor();
        }
        float bw = (ImGui::GetContentRegionAvail().x - 8.f) * 0.5f;
        if (d->enabled){
            ImGui::PushStyleColor(ImGuiCol_Button, T::v(IM_COL32(0x2E,0x3A,0x46,255)));
            if (ImGui::Button("Release", ImVec2(bw,0))) stop_device(d);
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, T::v(IM_COL32(0x1E,0x4D,0x3A,255)));
            if (ImGui::Button("Connect", ImVec2(bw,0))) start_device(d);
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        if (ImGui::Button("Set up", ImVec2(bw,0))){
            G.hw_dev = (int)i; G.hw_step = 0; G.hw_open = true; G.hw_base_ready = false;
            if (!d->enabled) start_device(d);
            g_engine.learn_ctrl.store(0);
        }
        if (d->can_switch_transport()){
            int tp = d->transport_get();
            ImGui::SetNextItemWidth(-1);
            const char* names[2] = { "HID direct read", "Raw Input" };
            int idx = (tp == BE_RAWINPUT) ? 1 : 0;
            if (ImGui::Combo("##tp", &idx, names, 2)){
                bool was = d->enabled;
                if (was) stop_device(d);
                d->transport_set(idx == 1 ? BE_RAWINPUT : BE_HID);
                if (was) start_device(d);
            }
        }
        ImGui::EndChild();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0,3));
    }
    if (ImGui::Button("Rescan controllers", ImVec2(-1,0))) rescan();

    ImGui::Dummy(ImVec2(0,6));
    section("STATE");
    int L = g_engine.layer_now.load();
    chip(L ? "LAYER 2" : "LAYER 1", L ? T::accent : IM_COL32(0x22,0x2A,0x33,255), L ? IM_COL32(0x0E,0x11,0x16,255) : T::dim);
    ImGui::SameLine();
    char tb[32]; snprintf(tb, sizeof tb, "transpose %+d", g_engine.transpose_now.load() + G.prof.transpose);
    chip(tb, IM_COL32(0x22,0x2A,0x33,255), T::dim);

    ImGui::Dummy(ImVec2(0,8));
    section("GLOBAL");
    ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dim));
    ImGui::TextUnformatted("transpose everything");
    ImGui::PopStyleColor();
    float sw = (ImGui::GetContentRegionAvail().x - 16.f) * 0.5f;
    if (ImGui::Button("-12", ImVec2(sw*0.6f,0))){ G.prof.transpose -= 12; commit(); }
    ImGui::SameLine(0,4);
    if (ImGui::Button("-1", ImVec2(sw*0.4f,0))){ G.prof.transpose--; commit(); }
    ImGui::SameLine(0,4);
    if (ImGui::Button("+1", ImVec2(sw*0.4f,0))){ G.prof.transpose++; commit(); }
    ImGui::SameLine(0,4);
    if (ImGui::Button("+12", ImVec2(sw*0.6f,0))){ G.prof.transpose += 12; commit(); }
    if (G.prof.transpose < -48) G.prof.transpose = -48;
    if (G.prof.transpose >  48) G.prof.transpose =  48;
    ImGui::Dummy(ImVec2(0,4));
    ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dim));
    ImGui::TextUnformatted("shortest gap between CC messages");
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##ccgap", &G.prof.cc_min_gap_us, 0, 8000, "%d us")) commit();
    if (ImGui::Checkbox("send MIDI", &G.prof.midi_enabled)) commit();
    ImGui::SameLine();
    if (ImGui::Checkbox("send keys", &G.prof.keys_enabled)) commit();
    ImGui::EndChild();
}

// -------------------------------------------------------- assignment panel ----
static void kind_picker(Action& a){
    static const char* kinds[] = { "Nothing", "MIDI note", "MIDI CC", "Pitch bend",
                                   "Program change", "Keyboard key", "Transpose",
                                   "Hold layer 2", "Panic (all notes off)" };
    int k = a.kind;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##kind", &k, kinds, ACT_KIND_COUNT)){ a.kind = k; commit(); }
}

static void assign_panel(float w){
    ImGui::BeginChild("assign", ImVec2(w, 0), ImGuiChildFlags_Borders);
    int c = G.sel;
    bool analog = control_is_axis(c);

    ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::accent));
    ImGui::TextUnformatted(control_name(c));
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dimmer));
    ImGui::TextUnformatted(analog ? "continuous control" : (control_is_dir(c) ? "axis used as a button" : "button"));
    ImGui::PopStyleColor();
    if (analog){
        int ax = c - C_AX_BEGIN;
        float v = axis_now(ax);
        ImGui::ProgressBar((v + (ax <= AX_RY ? 1.f : 0.f)) / (ax <= AX_RY ? 2.f : 1.f), ImVec2(-1, 6), "");
    }
    ImGui::Dummy(ImVec2(0,4));

    if (ImGui::BeginTabBar("layers")){
        for (int L = 0; L < LAYER_COUNT; L++){
            if (ImGui::BeginTabItem(L ? "Layer 2" : "Layer 1")){ G.layer_view = L; ImGui::EndTabItem(); }
        }
        ImGui::EndTabBar();
    }
    Action& a = G.prof.layer[G.layer_view][c];
    ImGui::Dummy(ImVec2(0,2));
    section("SENDS");
    kind_picker(a);
    ImGui::Dummy(ImVec2(0,4));

    char nb[16];
    switch (a.kind){
    case ACT_NOTE: {
        section("NOTE");
        ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::accent2));
        ImGui::Text("%s  (%d)", note_name(a.number, nb, sizeof nb), a.number);
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##note", &a.number, 0, 127, "%d")) commit();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##ch", &a.channel, 1, 16, "channel %d")) commit();
        if (!analog){
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##vel", &a.velocity, 1, 127, "velocity %d")) commit();
            if (ImGui::Checkbox("latch (press to start, press to stop)", &a.latch)) commit();
            section("VELOCITY FROM");
            const char* src[] = { "fixed", "left stick X", "left stick Y", "right stick X",
                                  "right stick Y", "left trigger", "right trigger" };
            int vs = a.vel_from + 1;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##vf", &vs, src, 7)){ a.vel_from = vs - 1; commit(); }
        } else {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##thr", &a.threshold, 0.05f, 0.95f, "trip at %.2f")) commit();
        }
        break;
    }
    case ACT_CC:
    case ACT_PROGRAM: {
        section(a.kind == ACT_CC ? "CONTROLLER" : "PROGRAM");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##num", &a.number, 0, 127, a.kind == ACT_CC ? "CC %d" : "program %d")) commit();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##ch", &a.channel, 1, 16, "channel %d")) commit();
        if (!analog){
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##on", &a.velocity, 0, 127, "pressed = %d")) commit();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##off", &a.off_value, 0, 127, "released = %d")) commit();
        }
        break;
    }
    case ACT_PITCHBEND:
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##ch", &a.channel, 1, 16, "channel %d")) commit();
        break;
    case ACT_KEY: {
        section("KEY");
        std::string kl = key_label(a.scancode, a.extended, 0);
        ImGui::PushStyleColor(ImGuiCol_Text, T::v(a.scancode ? T::accent2 : T::dimmer));
        ImGui::TextUnformatted(kl.c_str());
        ImGui::PopStyleColor();
        if (G.key_capture){
            ImGui::PushStyleColor(ImGuiCol_Button, T::v(T::accent));
            ImGui::PushStyleColor(ImGuiCol_Text, T::v(IM_COL32(0x0E,0x11,0x16,255)));
            if (ImGui::Button("press any key now...", ImVec2(-1,0))) G.key_capture = false;
            ImGui::PopStyleColor(2);
            if (G.cap_got){
                a.scancode = G.cap_sc; a.extended = G.cap_ext;
                G.cap_got = false; G.key_capture = false; commit();
            }
        } else {
            if (ImGui::Button("set key...", ImVec2(-1,0))){ G.key_capture = true; G.cap_got = false; }
        }
        bool m1 = a.mods & KMOD_CTRL, m2 = a.mods & KMOD_SHIFT, m3 = a.mods & KMOD_ALT, m4 = a.mods & KMOD_WIN;
        bool ch = false;
        ch |= ImGui::Checkbox("Ctrl", &m1); ImGui::SameLine();
        ch |= ImGui::Checkbox("Shift", &m2); ImGui::SameLine();
        ch |= ImGui::Checkbox("Alt", &m3); ImGui::SameLine();
        ch |= ImGui::Checkbox("Win", &m4);
        if (ch){
            a.mods = (m1?KMOD_CTRL:0)|(m2?KMOD_SHIFT:0)|(m3?KMOD_ALT:0)|(m4?KMOD_WIN:0);
            commit();
        }
        if (analog || control_is_dir(c)){
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##thr", &a.threshold, 0.05f, 0.95f, "trip at %.2f")) commit();
        }
        break;
    }
    case ACT_TRANSPOSE:
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##tr", &a.transpose, -24, 24, "%+d semitones")) commit();
        if (ImGui::Checkbox("latch", &a.latch)) commit();
        break;
    default: break;
    }

    if (analog && (a.kind == ACT_CC || a.kind == ACT_PITCHBEND || a.kind == ACT_PROGRAM)){
        ImGui::Dummy(ImVec2(0,6));
        section("SHAPE");
        if (ImGui::Checkbox("full travel (centre = middle of range)", &a.bipolar)) commit();
        if (ImGui::Checkbox("invert", &a.invert)) commit();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##dz", &a.deadzone, 0.f, 0.5f, "deadzone %.2f")) commit();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##cv", &a.curve, 0.3f, 3.f, "curve %.2f")) commit();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##lo", &a.out_min, 0.f, 1.f, "range low %.2f")) commit();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##hi", &a.out_max, 0.f, 1.f, "range high %.2f")) commit();
    }

    ImGui::Dummy(ImVec2(0,8));
    if (ImGui::Button("Clear this control", ImVec2(-1,0))){ a = Action(); a.kind = ACT_NONE; commit(); }
    static Action clip; static bool has_clip = false;
    float bw = (ImGui::GetContentRegionAvail().x - 8.f)*0.5f;
    if (ImGui::Button("Copy", ImVec2(bw,0))){ clip = a; has_clip = true; }
    ImGui::SameLine();
    if (ImGui::Button("Paste", ImVec2(bw,0)) && has_clip){ a = clip; commit(); }
    ImGui::EndChild();
}

// ----------------------------------------------------------- bottom panel -----
static void bottom_panel(float h){
    ImGui::BeginChild("bottom", ImVec2(0, h), ImGuiChildFlags_Borders);
    if (ImGui::BeginTabBar("btabs")){
        if (ImGui::BeginTabItem("Activity")){
            ImGui::BeginChild("logscroll", ImVec2(0,0));
            LogLine lines[64];
            int n = g_engine.log_copy(lines, 64);
            for (int i = 0; i < n; i++){
                ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dimmer));
                ImGui::Text("%7.2fs", lines[i].t);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextUnformatted(lines[i].text);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("All mappings")){
            if (ImGui::BeginTable("maps", 4, ImGuiTableFlags_RowBg|ImGuiTableFlags_Borders|
                                            ImGuiTableFlags_ScrollY|ImGuiTableFlags_SizingStretchProp)){
                ImGui::TableSetupColumn("layer", ImGuiTableColumnFlags_WidthFixed, 56.f);
                ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthFixed, 220.f);
                ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, 110.f);
                ImGui::TableSetupColumn("sends", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (int L = 0; L < LAYER_COUNT; L++)
                for (int c = 0; c < C_COUNT; c++){
                    const Action& a = G.prof.layer[L][c];
                    if (!a.assigned()) continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("%d", L+1);
                    ImGui::TableNextColumn();
                    char lbl[80]; snprintf(lbl, sizeof lbl, "%s##r%d_%d", control_name(c), L, c);
                    if (ImGui::Selectable(lbl, G.sel == c && G.layer_view == L)){ G.sel = c; G.layer_view = L; }
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(action_kind_name(a.kind));
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(action_summary(a).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Timing")){
            static float hist[240] = {};
            static int hi = 0;
            Device* d = nullptr;
            for (auto& x : G.devs) if (x->enabled){ d = x.get(); break; }
            if (d){
                hist[hi] = d->tel.last_interval_us.load() / 1000.f;
                hi = (hi + 1) % 240;
                char ov[64];
                snprintf(ov, sizeof ov, "report gap, ms   (%.0f Hz now)", d->tel.hz.load());
                ImGui::PlotLines("##hist", hist, 240, hi, ov, 0.f, 12.f, ImVec2(-1, 74));
                ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dim));
                ImGui::Text("gap min %.2f ms   max %.2f ms   jitter %.2f ms      "
                            "handler: last %u us, worst %u us      sampled once per frame",
                    d->tel.min_interval_us.load() == 0xFFFFFFFF ? 0.f : d->tel.min_interval_us.load()/1000.f,
                    d->tel.max_interval_us.load()/1000.f,
                    d->tel.jitter_ms.load(),
                    d->tel.last_process_us.load(), d->tel.max_process_us.load());
                ImGui::PopStyleColor();
                if (ImGui::Button("reset counters")){ d->tel.reset(); g_engine.worst_us.store(0); }
            } else ImGui::TextDisabled("connect a controller to see timing");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------- modals ------
static bool have_virtual_port(){
    for (auto& p : G.ports){
        std::string n = p.name;
        for (auto& ch : n) ch = (char)tolower((unsigned char)ch);
        if (n.find("loop") != std::string::npos || n.find("virtual") != std::string::npos ||
            n.find("lb") == 0) return true;
    }
    return false;
}

static void intro_modal(){
    if (!G.intro_open) return;
    ImGui::OpenPopup("welcome");
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(660, 540));
    if (!ImGui::BeginPopupModal("welcome", nullptr, ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoTitleBar))
        return;

    ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::accent));
    ImGui::TextUnformatted("ClaudeController");
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dim));
    ImGui::TextUnformatted("Three questions and you are playing.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0,6));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0,6));

    float body_h = 340.f;
    ImGui::BeginChild("introbody", ImVec2(0, body_h));
    if (G.intro_step == 0){
        section("1 / 3   WHICH CONTROLLER");
        if (G.devs.empty()){
            ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::warn));
            ImGui::TextWrapped("Nothing found. Plug the pad in, switch it on, then press Rescan.");
            ImGui::PopStyleColor();
        }
        if ((int)G.intro_pick.size() != (int)G.devs.size()) G.intro_pick.assign(G.devs.size(), 0);
        for (size_t i = 0; i < G.devs.size(); i++){
            auto& d = G.devs[i];
            ImGui::PushID((int)i);
            bool on = G.intro_pick[i] != 0;
            ImGui::BeginChild("row", ImVec2(0, 62), ImGuiChildFlags_Borders);
            if (ImGui::Checkbox("##pick", &on)) G.intro_pick[i] = on ? 1 : 0;
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(d->label.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dimmer));
            ImGui::Text("%s   %s", backend_name(d->backend), d->detail.c_str());
            ImGui::PopStyleColor();
            ImGui::EndGroup();
            ImGui::EndChild();
            ImGui::PopID();
        }
        ImGui::Dummy(ImVec2(0,4));
        if (ImGui::Button("Rescan")) rescan();
        ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dimmer));
        ImGui::TextWrapped("A pad can show up more than once: one entry per interface. "
                           "Pick the XInput entry for a reliable Xbox layout, or the HID entry "
                           "to read the pad's native report rate.");
        ImGui::PopStyleColor();
    } else if (G.intro_step == 1){
        section("2 / 3   WHAT SHOULD IT SEND");
        const char* t[3] = { "MIDI only", "Keyboard keys only", "Both" };
        const char* b[3] = { "Notes and CC into Ableton or any DAW through a virtual MIDI port.",
                             "Real keystrokes to whatever window has focus.",
                             "MIDI on some controls, keys on others." };
        for (int i = 0; i < 3; i++){
            bool sel = G.intro_purpose == i;
            ImGui::PushStyleColor(ImGuiCol_Button, T::v(sel ? IM_COL32(0x1E,0x3D,0x52,255) : IM_COL32(0x1E,0x24,0x2C,255)));
            if (ImGui::Button(t[i], ImVec2(-1, 40))) G.intro_purpose = i;
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dimmer));
            ImGui::TextWrapped("%s", b[i]);
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0,4));
        }
        if (G.intro_purpose != 1 && !have_virtual_port()){
            ImGui::Dummy(ImVec2(0,4));
            ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::accent2));
            ImGui::TextWrapped("No virtual MIDI port on this PC yet. Windows has no built-in one, so "
                               "install loopMIDI (free, from tobias-erichsen.de), add one port, and it "
                               "will show up in the MIDI out list. Everything else works without it.");
            ImGui::PopStyleColor();
        }
    } else {
        section("3 / 3   PICK A STARTING POINT");
        for (int i = 0; i < 4; i++){
            bool sel = G.intro_template == i;
            ImGui::PushStyleColor(ImGuiCol_Button, T::v(sel ? IM_COL32(0x1E,0x3D,0x52,255) : IM_COL32(0x1E,0x24,0x2C,255)));
            if (ImGui::Button(template_name(i), ImVec2(-1, 34))) G.intro_template = i;
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dimmer));
            ImGui::TextWrapped("%s", template_blurb(i));
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0,3));
        }
    }
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0,4));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0,4));
    if (G.intro_step > 0){ if (ImGui::Button("Back", ImVec2(110,34))) G.intro_step--; ImGui::SameLine(); }
    ImGui::SameLine(ImGui::GetWindowWidth() - 300.f);
    if (ImGui::Button("Skip setup", ImVec2(130,34))){
        G.intro_open = false; ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, T::v(IM_COL32(0x1E,0x5A,0x7A,255)));
    if (ImGui::Button(G.intro_step < 2 ? "Next" : "Start", ImVec2(130,34))){
        if (G.intro_step < 2) G.intro_step++;
        else {
            profile_template(G.prof, G.intro_template);
            if (G.intro_purpose == 0) G.prof.keys_enabled = false;
            if (G.intro_purpose == 1) G.prof.midi_enabled = false;
            snprintf(G.name_buf, sizeof G.name_buf, "%s", G.prof.name.c_str());
            for (size_t i = 0; i < G.devs.size(); i++)
                if (i < G.intro_pick.size() && G.intro_pick[i]) start_device(G.devs[i]);
            if (G.intro_purpose != 1 && G.port_sel < 0){
                for (auto& p : G.ports){
                    std::string n = p.name;
                    for (auto& ch : n) ch = (char)tolower((unsigned char)ch);
                    if (n.find("loop") != std::string::npos){
                        G.port_sel = p.index; G.prof.midi_port = p.name;
                        g_engine.set_midi_port(p.index); break;
                    }
                }
            }
            commit();
            G.intro_open = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::PopStyleColor();
    ImGui::EndPopup();
}

// order the setup wizard walks
static const int g_wiz[] = {
    C_A, C_B, C_X, C_Y, C_LB, C_RB, C_BACK, C_START, C_GUIDE, C_LS, C_RS,
    C_DU, C_DD, C_DL, C_DR, C_L4, C_R4, C_L5, C_R5,
    C_AX_LX, C_AX_LY, C_AX_RX, C_AX_RY, C_AX_LT, C_AX_RT
};
static const int g_wiz_n = (int)(sizeof g_wiz / sizeof g_wiz[0]);

static void hw_wizard(){
    if (!G.hw_open) return;
    ImGui::OpenPopup("hwsetup");
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Always, ImVec2(0.5f,0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 400));
    if (!ImGui::BeginPopupModal("hwsetup", nullptr, ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoTitleBar))
        return;
    auto close = [](){ G.hw_open = false; g_engine.learn_ctrl.store(-1); ImGui::CloseCurrentPopup(); };
    if (G.hw_dev < 0 || G.hw_dev >= (int)G.devs.size()){ close(); ImGui::EndPopup(); return; }
    auto& d = G.devs[G.hw_dev];
    DeviceProfile* dp = G.prof.ensure_device(d->key, d->label);

    ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::accent));
    ImGui::Text("Teach it your %s", d->label.c_str());
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dim));
    ImGui::TextWrapped("Nothing is sent out while this is open. Press or move what it asks for; "
                       "skip anything your pad does not have.");
    ImGui::PopStyleColor();
    ImGui::Separator();

    int ctrl = g_wiz[G.hw_step];
    bool is_ax = control_is_axis(ctrl);
    if (!G.hw_base_ready){ G.hw_base = d->last_raw; G.hw_base_ready = true; }

    ImGui::Dummy(ImVec2(0,10));
    ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dimmer));
    ImGui::Text("step %d of %d", G.hw_step + 1, g_wiz_n);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0,4));
    ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::accent2));
    ImGui::TextUnformatted(is_ax ? "Move" : "Press");
    ImGui::SameLine();
    ImGui::TextUnformatted(control_name(ctrl));
    ImGui::PopStyleColor();
    if (is_ax) ImGui::TextDisabled("push it all the way in one direction and hold");

    // watch for something new since the step began
    const RawSample& r = d->last_raw;
    int bound = -1;
    if (!is_ax){
        uint32_t fresh = r.buttons & ~G.hw_base.buttons;
        if (fresh){
            int i = 0; while (!((fresh >> i) & 1u)) i++;
            dp->btn[ctrl].kind = HW_BUTTON; dp->btn[ctrl].index = i;
            bound = i;
        } else if (r.hat >= 0 && G.hw_base.hat < 0 && ctrl >= C_DU && ctrl <= C_DR){
            int dir = r.hat & ~1;                  // snap a diagonal to its cardinal
            dp->btn[ctrl].kind = HW_HAT; dp->btn[ctrl].index = dir;
            bound = 100 + dir;
        }
    } else {
        int a = ctrl - C_AX_BEGIN;
        for (int i = 0; i < r.n_value && i < 16; i++){
            float delta = r.value[i] - (i < G.hw_base.n_value ? G.hw_base.value[i] : 0.5f);
            if (delta > 0.45f || delta < -0.45f){
                dp->axis[a].kind = HW_AXIS; dp->axis[a].index = i;
                dp->axis_invert[a] = (delta < 0);
                bound = i;
                break;
            }
        }
    }
    if (bound >= 0){
        commit();
        G.hw_step++;
        G.hw_base_ready = false;
        if (G.hw_step >= g_wiz_n){
            g_engine.log("%s: hardware layout learned", d->label.c_str());
            close();
        }
        ImGui::EndPopup();
        return;
    }

    // live raw view, so it is obvious the pad is talking
    ImGui::Dummy(ImVec2(0,12));
    section("RAW REPORT");
    char bits[40] = {};
    for (int i = 0; i < 16; i++) bits[i] = ((r.buttons >> i) & 1u) ? '1' : '.';
    ImGui::Text("buttons %s   hat %d", bits, r.hat);
    std::string axline;
    for (int i = 0; i < r.n_value && i < 8; i++){
        char t[32]; snprintf(t, sizeof t, "%s %.2f  ", d->axis_usage_name[i].c_str(), r.value[i]);
        axline += t;
    }
    ImGui::TextUnformatted(axline.c_str());

    ImGui::Dummy(ImVec2(0,10));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0,4));
    if (G.hw_step > 0){ if (ImGui::Button("Back", ImVec2(100,32))){ G.hw_step--; G.hw_base_ready = false; } ImGui::SameLine(); }
    if (ImGui::Button("Skip", ImVec2(100,32))){
        if (!is_ax) dp->btn[ctrl] = HwBind(); else dp->axis[ctrl - C_AX_BEGIN] = HwBind();
        commit();
        G.hw_step++; G.hw_base_ready = false;
        if (G.hw_step >= g_wiz_n){ close(); ImGui::EndPopup(); return; }
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 130.f);
    if (ImGui::Button("Done", ImVec2(110,32))){ close(); ImGui::EndPopup(); return; }
    ImGui::EndPopup();
}

// ------------------------------------------------------------------- frame ----
void gui_init(){
    apply_style();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    const char* fonts[] = { "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\tahoma.ttf" };
    for (const char* f : fonts){
        if (GetFileAttributesA(f) == INVALID_FILE_ATTRIBUTES) continue;
        if (io.Fonts->AddFontFromFileTTF(f, 17.0f)) break;
    }

    refresh_ports();
    rescan();
    std::string p = default_profile_path();
    if (profile_load(G.prof, p)){
        G.profile_path = p;
        snprintf(G.name_buf, sizeof G.name_buf, "%s", G.prof.name.c_str());
        g_engine.log("profile loaded from disk");
        G.intro_open = false;
        for (auto& pt : G.ports)
            if (!G.prof.midi_port.empty() && pt.name == G.prof.midi_port){
                G.port_sel = pt.index; g_engine.set_midi_port(pt.index);
            }
        for (auto& d : G.devs) if (G.prof.find_device(d->key)) start_device(d);
    } else {
        profile_template(G.prof, 1);
        snprintf(G.name_buf, sizeof G.name_buf, "%s", G.prof.name.c_str());
    }
    commit();
    G.dirty = false;
}

void gui_shutdown(){
    for (auto& d : G.devs) stop_device(d);
    G.prof.name = G.name_buf;
    // always to the session file, so an explicit "save as" copy is never clobbered
    profile_save(G.prof, default_profile_path());
}

void gui_on_key(uint16_t scancode, bool extended, bool down, uint32_t vk){
    (void)vk;
    if (down && G.key_capture && scancode){
        G.cap_sc = scancode; G.cap_ext = extended; G.cap_got = true;
    }
}
bool gui_wants_quit(){ return G.quit; }

static void autosave_tick(){
    static double last_change = -1.0;
    static bool   was_dirty = false;
    double now = ImGui::GetTime();
    if (G.dirty && !was_dirty) last_change = now;
    was_dirty = G.dirty;
    if (G.dirty && last_change > 0 && now - last_change > 3.0){
        G.prof.name = G.name_buf;
        profile_save(G.prof, default_profile_path());
        G.dirty = false;
        was_dirty = false;
    }
}

void gui_frame(){
    autosave_tick();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("root", nullptr,
        ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float top_h = 58.f, bot_h = 150.f;
    top_bar(top_h);

    float mid_h = ImGui::GetContentRegionAvail().y - bot_h - ImGui::GetStyle().ItemSpacing.y;
    if (mid_h < 220.f) mid_h = 220.f;
    ImGui::BeginChild("mid", ImVec2(0, mid_h));
    const float left_w = 292.f, right_w = 338.f;
    devices_panel(left_w);
    ImGui::SameLine();
    float center_w = ImGui::GetContentRegionAvail().x - right_w - ImGui::GetStyle().ItemSpacing.x;
    ImGui::BeginChild("center", ImVec2(center_w, 0), ImGuiChildFlags_Borders);
    {
        for (int L = 0; L < LAYER_COUNT; L++){
            bool sel = G.layer_view == L;
            ImGui::PushStyleColor(ImGuiCol_Button, T::v(sel ? IM_COL32(0x1E,0x3D,0x52,255) : IM_COL32(0x1E,0x24,0x2C,255)));
            ImGui::PushStyleColor(ImGuiCol_Text, T::v(sel ? T::accent : T::dim));
            if (ImGui::Button(L ? "Layer 2 (hold)" : "Layer 1 (base)", ImVec2(128,26))) G.layer_view = L;
            ImGui::PopStyleColor(2);
            if (L == 0) ImGui::SameLine();
        }
        ImGui::SameLine(0, 14);
        ImGui::Checkbox("labels", &G.show_labels);
        ImGui::SameLine(0, 14);
        ImGui::PushStyleColor(ImGuiCol_Text, T::v(T::dimmer));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("click a control, then set what it sends");
        ImGui::PopStyleColor();

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float chips_h = 76.f;
        float pad_w = avail.x;
        float pad_h = pad_w * 0.70f;
        if (pad_h > avail.y - chips_h){ pad_h = avail.y - chips_h; pad_w = pad_h / 0.70f; }
        ImVec2 col = ImGui::GetCursorScreenPos();
        ImVec2 origin(col.x + (avail.x - pad_w)*0.5f, col.y);
        draw_pad(origin, ImVec2(pad_w, pad_h), G.show_labels);
        ImGui::SetCursorScreenPos(ImVec2(col.x, origin.y + pad_h + 4.f));
        ImGui::BeginChild("chips", ImVec2(0,0));
        axis_chips();
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    assign_panel(right_w);
    ImGui::EndChild();

    bottom_panel(bot_h);
    ImGui::End();

    intro_modal();
    hw_wizard();
}
