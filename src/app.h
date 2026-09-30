// ClaudeController - gamepad to MIDI / keyboard bridge, built for low latency.
// Shared declarations.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#include <mmsystem.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <atomic>

// ---------------------------------------------------------------- timing ----
uint64_t qpc_now();
double   qpc_to_ms(uint64_t ticks);      // tick delta -> milliseconds

// -------------------------------------------------------------- controls ----
// Logical controls. Buttons first, then continuous axes, then axis directions
// (an axis pushed past a threshold, so a stick can act like four buttons).
enum : int {
    C_A = 0, C_B, C_X, C_Y,
    C_LB, C_RB,
    C_BACK, C_START, C_GUIDE,
    C_LS, C_RS,
    C_DU, C_DD, C_DL, C_DR,
    C_L4, C_R4, C_L5, C_R5,
    C_BTN_COUNT,                                   // 19 digital buttons

    C_AX_BEGIN = C_BTN_COUNT,
    C_AX_LX = C_AX_BEGIN, C_AX_LY, C_AX_RX, C_AX_RY, C_AX_LT, C_AX_RT,
    C_AX_END,                                      // 25
    AX_COUNT = C_AX_END - C_AX_BEGIN,              // 6 continuous axes

    C_DIR_BEGIN = C_AX_END,
    DIR_COUNT = 10,                                // LX-+ LY-+ RX-+ RY-+ LT+ RT+
    C_COUNT = C_DIR_BEGIN + DIR_COUNT              // 35 assignable controls
};
enum { AX_LX = 0, AX_LY, AX_RX, AX_RY, AX_LT, AX_RT };

const char* control_name(int ctrl);
const char* control_short(int ctrl);
bool        control_is_axis(int ctrl);       // continuous
bool        control_is_dir(int ctrl);        // axis-as-button
void        dir_decode(int ctrl, int* axis, int* sign);

// --------------------------------------------------------------- actions ----
enum ActionKind : int {
    ACT_NONE = 0,
    ACT_NOTE,          // digital: note on/off   axis: note with velocity from axis
    ACT_CC,            // digital: on/off value  axis: continuous
    ACT_PITCHBEND,     // axis only
    ACT_PROGRAM,
    ACT_KEY,
    ACT_TRANSPOSE,     // shift every note action by N semitones while held (or latched)
    ACT_LAYER,         // hold for layer 2
    ACT_PANIC,
    ACT_KIND_COUNT
};
const char* action_kind_name(int k);

enum { KMOD_CTRL = 1, KMOD_SHIFT = 2, KMOD_ALT = 4, KMOD_WIN = 8 };

struct Action {
    int   kind      = ACT_NONE;
    int   channel   = 1;      // 1..16
    int   number    = 60;     // note / cc / program number
    int   velocity  = 100;    // note velocity, or CC value when pressed
    int   off_value = 0;      // CC value when released
    bool  latch     = false;  // digital: toggle instead of hold
    int   transpose = 12;     // ACT_TRANSPOSE semitones
    // keyboard
    uint16_t scancode = 0;
    bool     extended = false;
    uint32_t mods     = 0;
    bool     key_repeat = false;
    // analog shaping
    float deadzone  = 0.10f;
    float out_min   = 0.0f;   // 0..1 of the CC / bend range
    float out_max   = 1.0f;
    float curve     = 1.0f;   // 1 = linear, <1 fast rise, >1 slow rise
    bool  invert    = false;
    bool  bipolar   = false;  // stick: use full -1..1 (centre = middle of range)
    float threshold = 0.5f;   // axis-as-button trip point
    int   vel_from  = -1;     // axis index used as note velocity source, -1 = fixed

    bool assigned() const { return kind != ACT_NONE; }
};

// --------------------------------------------------- per-device hardware ----
// Which raw HID button / axis feeds each logical control.
enum { HW_NONE = 0, HW_BUTTON, HW_AXIS, HW_HAT };
struct HwBind {
    int kind  = HW_NONE;
    int index = -1;      // raw button bit, raw axis slot, or hat direction 0..7
};

struct DeviceProfile {
    std::string key;                       // stable device id
    std::string label;
    HwBind btn[C_BTN_COUNT];
    HwBind axis[AX_COUNT];
    bool   axis_invert[AX_COUNT]   = {};
    float  axis_deadzone[AX_COUNT] = {0.12f, 0.12f, 0.12f, 0.12f, 0.02f, 0.02f};
};

// --------------------------------------------------------------- profile ----
#define LAYER_COUNT 2

struct Profile {
    std::string name = "Default";
    std::string midi_port;                 // matched by substring
    Action      layer[LAYER_COUNT][C_COUNT];
    int         transpose   = 0;           // global, semitones
    int         cc_min_gap_us = 500;       // CC rate limit per control
    bool        keys_enabled = true;
    bool        midi_enabled = true;
    std::vector<DeviceProfile> devices;

    DeviceProfile* find_device(const std::string& key);
    DeviceProfile* ensure_device(const std::string& key, const std::string& label);
};

bool profile_save(const Profile& p, const std::string& path);
bool profile_load(Profile& p, const std::string& path);
void profile_template(Profile& p, int which);   // 0 blank 1 drums 2 chromatic 3 keyboard
const char* template_name(int which);
const char* template_blurb(int which);

// ------------------------------------------------------------ midi / key ----
class MidiOut {
public:
    bool  open(int device_index);
    void  close();
    bool  is_open() const { return h_ != nullptr; }
    int   index() const { return index_; }
    // hot path, called from device threads
    void  note_on (int ch, int note, int vel);
    void  note_off(int ch, int note);
    void  cc      (int ch, int num, int val);
    void  program (int ch, int num);
    void  bend    (int ch, int value14);
    void  panic();
    std::atomic<uint64_t> sent{0};
private:
    void  send(uint32_t msg);
    HMIDIOUT h_ = nullptr;
    int      index_ = -1;
    uint8_t  active_[16][128] = {};     // note ref counts, for clean panic
};

struct MidiPortInfo { int index; std::string name; };
std::vector<MidiPortInfo> midi_list_outputs();

void key_send(uint16_t scancode, bool extended, uint32_t mods, bool down);

// ---------------------------------------------------------------- device ----
struct Telemetry {
    std::atomic<uint64_t> reports{0};
    std::atomic<uint32_t> last_interval_us{0};
    std::atomic<uint32_t> min_interval_us{0xFFFFFFFF};
    std::atomic<uint32_t> max_interval_us{0};
    std::atomic<uint32_t> last_process_us{0};
    std::atomic<uint32_t> max_process_us{0};
    std::atomic<float>    hz{0.f};
    std::atomic<float>    jitter_ms{0.f};
    void reset();
};

enum Backend { BE_HID = 0, BE_RAWINPUT, BE_XINPUT, BE_COUNT };
const char* backend_name(int b);

struct RawSample {                 // what the hardware actually reported
    uint32_t buttons = 0;          // bit per raw HID button
    float    value[16] = {};       // raw axes normalised 0..1
    int      n_value = 0;
    int      hat = -1;             // 0..7 clockwise from up, -1 centred
};

struct NormState {                 // after hardware binding
    uint32_t buttons = 0;          // bit per C_BTN_*
    float    axis[AX_COUNT] = {};  // sticks -1..1, triggers 0..1
};

// Runtime state per logical control, owned by the device thread that writes it.
struct CtrlRT {
    int      press_layer = -1;   // layer the press was fired on
    bool     latched  = false;
    int      held_note = -1, held_chan = 1;
    int      last_out  = -1;     // last CC / bend value emitted
    uint64_t last_send = 0;
    bool     dir_down  = false;
};

class Engine;

class Device {
public:
    virtual ~Device() {}
    virtual bool start(Engine* eng) = 0;
    virtual void stop() = 0;
    virtual bool can_switch_transport() const { return false; }
    virtual int  transport_get() const { return backend; }
    virtual void transport_set(int) {}

    std::string key, label, detail;
    int         backend = BE_HID;
    int         n_raw_buttons = 0, n_raw_axes = 0;
    bool        has_hat = false;
    bool        fixed_layout = false;     // XInput: logical layout already known
    std::string axis_usage_name[16];
    bool        enabled = false;
    Telemetry   tel;
    RawSample   last_raw;                 // for the GUI / learn wizard
    NormState   last_norm;
    std::atomic<uint32_t> raw_seq{0};     // bumps on every report
    std::string error;
    CtrlRT      rt[C_COUNT];
    int         prof_index = -1;          // cached slot in Profile::devices
};

std::vector<std::shared_ptr<Device>> enumerate_devices();

// ---------------------------------------------------------------- engine ----
struct LogLine { double t; char text[96]; };

class Engine {
public:
    void  init();
    void  shutdown();
    // called from device threads with the timestamp taken at report arrival
    void  on_report(Device* d, const RawSample& raw, uint64_t stamp);

    void  publish(const Profile& p);       // GUI -> engine
    void  snapshot(Profile& out);          // engine -> GUI
    void  panic();
    void  set_midi_port(int index);
    void  set_active(bool on) { active_.store(on, std::memory_order_relaxed); }
    bool  active() const { return active_.load(std::memory_order_relaxed); }

    MidiOut midi;
    std::atomic<int>      layer_now{0};
    std::atomic<int>      transpose_now{0};
    std::atomic<uint64_t> events{0};
    std::atomic<uint32_t> worst_us{0};

    // learn: when >= 0 the engine passes reports through untouched and the GUI
    // reads Device::last_raw to bind hardware.
    std::atomic<int> learn_ctrl{-1};

    void   log(const char* fmt, ...);
    int    log_copy(LogLine* out, int max);

private:
    void  normalise(Device* d, const DeviceProfile* dp, const RawSample& raw, NormState& ns);
    void  fire(Device* d, int ctrl, const Action& a, bool down, float analog);
    void  analog_update(Device* d, int ctrl, const Action& a, float v, uint64_t stamp);

    SRWLOCK      lock_;
    Profile      prof_;
    std::atomic<bool> active_{true};

    CRITICAL_SECTION log_cs_;
    LogLine  log_[128];
    int      log_head_ = 0, log_count_ = 0;
    uint64_t t0_ = 0;
};

extern Engine g_engine;

// ------------------------------------------------------------------- gui ----
void gui_init();
void gui_frame();
bool gui_wants_quit();
void gui_on_key(uint16_t scancode, bool extended, bool down, uint32_t vk);
void gui_shutdown();

std::string exe_dir();
