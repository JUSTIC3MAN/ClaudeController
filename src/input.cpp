#include "app.h"
#include <setupapi.h>
#include <xinput.h>
#include <cstdio>
#include <thread>
#include <algorithm>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// ======================================================== HID report parse ==
struct AxisCap {
    USAGE  usage = 0;
    LONG   lmin = 0, lmax = 0;
    USHORT bits = 0;
    USHORT link = 0;
    bool   signed_ = false;
};

struct HidParse {
    PHIDP_PREPARSED_DATA pp = nullptr;
    HIDP_CAPS            caps = {};
    std::vector<AxisCap> axes;        // generic desktop values, hat excluded
    AxisCap              hat;
    bool                 has_hat = false;
    ULONG                max_usages = 0;
    std::vector<USAGE>   usage_buf;

    bool build(){
        if (HidP_GetCaps(pp, &caps) != HIDP_STATUS_SUCCESS) return false;
        if (caps.NumberInputValueCaps){
            std::vector<HIDP_VALUE_CAPS> v(caps.NumberInputValueCaps);
            USHORT n = caps.NumberInputValueCaps;
            if (HidP_GetValueCaps(HidP_Input, v.data(), &n, pp) == HIDP_STATUS_SUCCESS){
                for (USHORT i = 0; i < n; i++){
                    const HIDP_VALUE_CAPS& c = v[i];
                    if (c.UsagePage != 0x01) continue;
                    AxisCap a;
                    a.usage = c.IsRange ? c.Range.UsageMin : c.NotRange.Usage;
                    a.lmin = c.LogicalMin; a.lmax = c.LogicalMax;
                    a.bits = c.BitSize; a.link = c.LinkCollection;
                    a.signed_ = c.LogicalMin < 0;
                    if (a.lmax <= a.lmin){                       // unspecified range
                        a.lmin = 0;
                        a.lmax = (a.bits >= 32) ? 0x7FFFFFFF : ((1L << a.bits) - 1);
                        a.signed_ = false;
                    }
                    if (a.usage == 0x39){ hat = a; has_hat = true; }
                    else if (a.usage >= 0x30 && a.usage <= 0x38 && axes.size() < 16) axes.push_back(a);
                }
            }
        }
        max_usages = HidP_MaxUsageListLength(HidP_Input, 0x09, pp);
        if (max_usages == 0) max_usages = 32;
        usage_buf.resize(max_usages);
        return true;
    }

    // Decode one input report into a RawSample. Runs on the hot path: no allocation.
    bool decode(PCHAR report, ULONG len, RawSample& out, int* n_buttons_seen){
        out.buttons = 0;
        ULONG n = max_usages;
        if (HidP_GetUsages(HidP_Input, 0x09, 0, usage_buf.data(), &n, pp, report, len) == HIDP_STATUS_SUCCESS){
            for (ULONG i = 0; i < n; i++){
                USAGE u = usage_buf[i];
                if (u >= 1 && u <= 32) out.buttons |= 1u << (u - 1);
            }
        }
        if (n_buttons_seen) *n_buttons_seen = (int)max_usages;

        out.n_value = (int)axes.size();
        for (size_t i = 0; i < axes.size(); i++){
            const AxisCap& a = axes[i];
            ULONG raw = 0;
            if (HidP_GetUsageValue(HidP_Input, 0x01, a.link, a.usage, &raw, pp, report, len) != HIDP_STATUS_SUCCESS){
                out.value[i] = 0.5f; continue;
            }
            double v;
            if (a.signed_ && a.bits > 0 && a.bits < 32){
                ULONG sign = 1UL << (a.bits - 1);
                LONG  sv = (raw & sign) ? (LONG)(raw | ~((1UL << a.bits) - 1)) : (LONG)raw;
                v = (double)(sv - a.lmin) / (double)(a.lmax - a.lmin);
            } else {
                v = (double)((LONG)raw - a.lmin) / (double)(a.lmax - a.lmin);
            }
            if (v < 0) v = 0;
            if (v > 1) v = 1;
            out.value[i] = (float)v;
        }
        out.hat = -1;
        if (has_hat){
            ULONG raw = 0;
            if (HidP_GetUsageValue(HidP_Input, 0x01, hat.link, 0x39, &raw, pp, report, len) == HIDP_STATUS_SUCCESS){
                LONG v = (LONG)raw;
                LONG span = hat.lmax - hat.lmin + 1;
                if (v >= hat.lmin && v <= hat.lmax && span >= 4){
                    LONG d = v - hat.lmin;
                    out.hat = (span == 4) ? (int)(d * 2) : (int)((d * 8) / span);
                }
            }
        }
        return true;
    }
};

static const char* usage_label(USAGE u){
    switch (u){
        case 0x30: return "X";      case 0x31: return "Y";
        case 0x32: return "Z";      case 0x33: return "Rx";
        case 0x34: return "Ry";     case 0x35: return "Rz";
        case 0x36: return "Slider"; case 0x37: return "Dial";
        case 0x38: return "Wheel";
    }
    return "?";
}

// ============================================================== pad device ==
class PadDevice : public Device {
public:
    std::string path;
    HANDLE      h = INVALID_HANDLE_VALUE;
    bool        read_ok = false;         // handle has read access
    HidParse    parse;
    int         transport = BE_HID;

    bool open_probe(const char* dev_path){
        path = dev_path;
        h = CreateFileA(dev_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        read_ok = (h != INVALID_HANDLE_VALUE);
        if (!read_ok){
            // XInput-class pads refuse read access; metadata still works with 0 access
            h = CreateFileA(dev_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) return false;
        }
        if (!HidD_GetPreparsedData(h, &parse.pp)) return false;
        if (!parse.build()) return false;
        if (parse.caps.UsagePage != 0x01) return false;
        if (parse.caps.Usage != 0x04 && parse.caps.Usage != 0x05 && parse.caps.Usage != 0x08) return false;

        n_raw_axes    = (int)parse.axes.size();
        n_raw_buttons = (int)parse.max_usages;
        has_hat       = parse.has_hat;
        for (size_t i = 0; i < parse.axes.size(); i++) axis_usage_name[i] = usage_label(parse.axes[i].usage);
        transport = read_ok ? BE_HID : BE_RAWINPUT;
        backend   = transport;
        return true;
    }

    ~PadDevice() override {
        stop();
        if (parse.pp) HidD_FreePreparsedData(parse.pp);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    bool start(Engine* eng) override;
    void stop() override;
    bool can_switch_transport() const override { return read_ok; }
    int  transport_get() const override { return transport; }
    void transport_set(int t) override { transport = (t == BE_RAWINPUT) ? BE_RAWINPUT : BE_HID; backend = transport; }

    void feed(PCHAR report, ULONG len, uint64_t stamp);

    Engine* eng_ = nullptr;
    HANDLE  quit_ = nullptr;
    std::thread th_;
    uint64_t last_stamp_ = 0;
    double   hz_acc_ = 0.0;
    double   jit_acc_ = 0.0;
    RawSample scratch_;
    HANDLE    ri_handle = nullptr;
};

// ------------------------------------------------------------- raw input hub --
// One thread, one message-only window, every Raw Input pad dispatched from it.
// Its own tight GetMessage loop keeps WM_INPUT off the GUI thread, so report
// handling never waits on a repaint.
class RawInputHub {
public:
    static RawInputHub& get(){ static RawInputHub h; return h; }

    void add(PadDevice* d){
        EnterCriticalSection(&cs_);
        devs_.push_back(d);
        LeaveCriticalSection(&cs_);
        ensure_thread();
    }
    void remove(PadDevice* d){
        EnterCriticalSection(&cs_);
        devs_.erase(std::remove(devs_.begin(), devs_.end(), d), devs_.end());
        LeaveCriticalSection(&cs_);
    }

private:
    RawInputHub(){ InitializeCriticalSection(&cs_); }

    void ensure_thread(){
        if (started_) return;
        started_ = true;
        th_ = std::thread([this]{ run(); });
        th_.detach();
    }

    static LRESULT CALLBACK proc(HWND hw, UINT m, WPARAM wp, LPARAM lp){
        if (m == WM_INPUT){
            uint64_t stamp = qpc_now();
            RawInputHub::get().on_input((HRAWINPUT)lp, stamp);
            return 0;
        }
        return DefWindowProcW(hw, m, wp, lp);
    }

    void on_input(HRAWINPUT hri, uint64_t stamp){
        UINT size = 0;
        if (GetRawInputData(hri, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0) return;
        if (size > sizeof(buf_)) return;
        if (GetRawInputData(hri, RID_INPUT, buf_, &size, sizeof(RAWINPUTHEADER)) != size) return;
        RAWINPUT* ri = (RAWINPUT*)buf_;
        if (ri->header.dwType != RIM_TYPEHID) return;

        PadDevice* target = nullptr;
        EnterCriticalSection(&cs_);
        for (PadDevice* d : devs_) if (d->ri_handle == ri->header.hDevice){ target = d; break; }
        if (!target){
            // resolve handle -> path once, then cache
            char name[512] = {};
            UINT n = sizeof name;
            if (GetRawInputDeviceInfoA(ri->header.hDevice, RIDI_DEVICENAME, name, &n) > 0){
                for (PadDevice* d : devs_)
                    if (_stricmp(d->path.c_str(), name) == 0){ d->ri_handle = ri->header.hDevice; target = d; break; }
            }
        }
        LeaveCriticalSection(&cs_);
        if (!target) return;

        DWORD count = ri->data.hid.dwCount, len = ri->data.hid.dwSizeHid;
        BYTE* p = (BYTE*)ri->data.hid.bRawData;
        for (DWORD i = 0; i < count; i++) target->feed((PCHAR)(p + i * len), len, stamp);
    }

    void run(){
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"ClaudeControllerRawInput";
        RegisterClassExW(&wc);
        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        RAWINPUTDEVICE rid[3] = {};
        rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x04;   // gamepad
        rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x05;   // joystick
        rid[2].usUsagePage = 0x01; rid[2].usUsage = 0x08;   // multi-axis
        for (int i = 0; i < 3; i++){ rid[i].dwFlags = RIDEV_INPUTSINK; rid[i].hwndTarget = hwnd_; }
        RegisterRawInputDevices(rid, 3, sizeof(RAWINPUTDEVICE));

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0){
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    CRITICAL_SECTION cs_;
    std::vector<PadDevice*> devs_;
    std::thread th_;
    bool  started_ = false;
    HWND  hwnd_ = nullptr;
    BYTE  buf_[4096];
};

// ---------------------------------------------------- pad device transport --
void PadDevice::feed(PCHAR report, ULONG len, uint64_t stamp){
    if (!parse.decode(report, len, scratch_, nullptr)) return;

    if (last_stamp_){
        double dt_ms = qpc_to_ms(stamp - last_stamp_);
        uint32_t us = (uint32_t)(dt_ms * 1000.0);
        tel.last_interval_us.store(us, std::memory_order_relaxed);
        if (us < tel.min_interval_us.load(std::memory_order_relaxed)) tel.min_interval_us.store(us, std::memory_order_relaxed);
        if (us > tel.max_interval_us.load(std::memory_order_relaxed)) tel.max_interval_us.store(us, std::memory_order_relaxed);
        // smoothed rate and mean absolute deviation
        hz_acc_  = hz_acc_  * 0.995 + dt_ms * 0.005;
        jit_acc_ = jit_acc_ * 0.99  + (dt_ms > hz_acc_ ? dt_ms - hz_acc_ : hz_acc_ - dt_ms) * 0.01;
        if (hz_acc_ > 0.0001) tel.hz.store((float)(1000.0 / hz_acc_), std::memory_order_relaxed);
        tel.jitter_ms.store((float)jit_acc_, std::memory_order_relaxed);
    }
    last_stamp_ = stamp;
    tel.reports.fetch_add(1, std::memory_order_relaxed);

    last_raw = scratch_;
    raw_seq.fetch_add(1, std::memory_order_release);

    if (eng_) eng_->on_report(this, scratch_, stamp);

    uint32_t proc_us = (uint32_t)(qpc_to_ms(qpc_now() - stamp) * 1000.0);
    tel.last_process_us.store(proc_us, std::memory_order_relaxed);
    if (proc_us > tel.max_process_us.load(std::memory_order_relaxed))
        tel.max_process_us.store(proc_us, std::memory_order_relaxed);
}

bool PadDevice::start(Engine* eng){
    eng_ = eng;
    backend = transport;
    tel.reset();
    last_stamp_ = 0; hz_acc_ = 0; jit_acc_ = 0;

    if (transport == BE_RAWINPUT){
        RawInputHub::get().add(this);
        enabled = true;
        return true;
    }
    if (!read_ok){
        error = "no read access to this device, use Raw Input";
        return false;
    }
    // Ask the driver for the shallowest input queue it allows: if we ever fall
    // behind, we want the newest report, not a backlog of stale ones.
    HidD_SetNumInputBuffers(h, 2);

    quit_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    enabled = true;
    th_ = std::thread([this]{
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        const ULONG rlen = parse.caps.InputReportByteLength ? parse.caps.InputReportByteLength : 64;
        std::vector<char> buf(rlen + 8);
        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE waits[2] = { ov.hEvent, quit_ };
        while (enabled){
            ResetEvent(ov.hEvent);
            DWORD got = 0;
            BOOL ok = ReadFile(h, buf.data(), rlen, &got, &ov);
            if (!ok){
                DWORD e = GetLastError();
                if (e != ERROR_IO_PENDING){ error = "read failed"; break; }
                DWORD wr = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (wr != WAIT_OBJECT_0) { CancelIo(h); break; }
                if (!GetOverlappedResult(h, &ov, &got, FALSE)) break;
            }
            if (got == 0) continue;
            feed(buf.data(), got, qpc_now());
        }
        CloseHandle(ov.hEvent);
    });
    return true;
}

void PadDevice::stop(){
    if (!enabled) return;
    enabled = false;
    if (transport == BE_RAWINPUT){ RawInputHub::get().remove(this); return; }
    if (quit_) SetEvent(quit_);
    CancelIoEx(h, nullptr);
    if (th_.joinable()) th_.join();
    if (quit_){ CloseHandle(quit_); quit_ = nullptr; }
}

// ============================================================ xinput device ==
typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD, XINPUT_STATE*);
static PFN_XInputGetState g_xget = nullptr;
static bool xinput_load(){
    if (g_xget) return true;
    const wchar_t* dlls[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
    for (const wchar_t* d : dlls){
        HMODULE m = LoadLibraryW(d);
        if (!m) continue;
        // Ordinal 100 is XInputGetStateEx, the only entry point that reports the
        // Guide button. Fall back to the documented one if it is not there.
        g_xget = (PFN_XInputGetState)GetProcAddress(m, (LPCSTR)(uintptr_t)100);
        if (!g_xget) g_xget = (PFN_XInputGetState)GetProcAddress(m, "XInputGetState");
        if (g_xget) return true;
    }
    return false;
}

class XInputDevice : public Device {
public:
    ~XInputDevice() override { stop(); }
    int slot = 0;
    bool start(Engine* eng) override {
        if (!xinput_load()) { error = "XInput not available"; return false; }
        eng_ = eng;
        backend = BE_XINPUT;
        tel.reset();
        last_stamp_ = 0; hz_acc_ = 0; jit_acc_ = 0;
        quit_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        enabled = true;
        th_ = std::thread([this]{ run(); });
        return true;
    }
    void stop() override {
        if (!enabled) return;
        enabled = false;
        if (quit_) SetEvent(quit_);
        if (th_.joinable()) th_.join();
        if (quit_){ CloseHandle(quit_); quit_ = nullptr; }
    }
private:
    void run(){
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        // A high-resolution waitable timer gives a true 1 ms cadence without
        // burning a core on a spin loop.
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr,
                          CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        LARGE_INTEGER due; due.QuadPart = -10000;      // 1 ms
        if (timer) SetWaitableTimer(timer, &due, 1, nullptr, nullptr, FALSE);
        HANDLE waits[2] = { timer, quit_ };
        DWORD last_packet = 0;
        while (enabled){
            if (timer){
                if (WaitForMultipleObjects(2, waits, FALSE, 50) == WAIT_OBJECT_0 + 1) break;
            } else {
                if (WaitForSingleObject(quit_, 1) == WAIT_OBJECT_0) break;
            }
            XINPUT_STATE st = {};
            if (g_xget((DWORD)slot, &st) != ERROR_SUCCESS){ Sleep(200); continue; }
            if (st.dwPacketNumber == last_packet) continue;
            last_packet = st.dwPacketNumber;
            uint64_t stamp = qpc_now();

            RawSample rs;
            const WORD b = st.Gamepad.wButtons;
            auto bit = [&](int i, bool on){ if (on) rs.buttons |= 1u << i; };
            bit(0,  b & XINPUT_GAMEPAD_A);
            bit(1,  b & XINPUT_GAMEPAD_B);
            bit(2,  b & XINPUT_GAMEPAD_X);
            bit(3,  b & XINPUT_GAMEPAD_Y);
            bit(4,  b & XINPUT_GAMEPAD_LEFT_SHOULDER);
            bit(5,  b & XINPUT_GAMEPAD_RIGHT_SHOULDER);
            bit(6,  b & XINPUT_GAMEPAD_BACK);
            bit(7,  b & XINPUT_GAMEPAD_START);
            bit(8,  b & 0x0400);                       // guide (undocumented bit)
            bit(9,  b & XINPUT_GAMEPAD_LEFT_THUMB);
            bit(10, b & XINPUT_GAMEPAD_RIGHT_THUMB);
            bit(11, b & XINPUT_GAMEPAD_DPAD_UP);
            bit(12, b & XINPUT_GAMEPAD_DPAD_DOWN);
            bit(13, b & XINPUT_GAMEPAD_DPAD_LEFT);
            bit(14, b & XINPUT_GAMEPAD_DPAD_RIGHT);
            // 0..1, Y flipped so "up" is the low end on every backend
            rs.value[0] = (st.Gamepad.sThumbLX + 32768.f) / 65535.f;
            rs.value[1] = 1.f - (st.Gamepad.sThumbLY + 32768.f) / 65535.f;
            rs.value[2] = (st.Gamepad.sThumbRX + 32768.f) / 65535.f;
            rs.value[3] = 1.f - (st.Gamepad.sThumbRY + 32768.f) / 65535.f;
            rs.value[4] = st.Gamepad.bLeftTrigger / 255.f;
            rs.value[5] = st.Gamepad.bRightTrigger / 255.f;
            rs.n_value = 6;
            rs.hat = -1;

            if (last_stamp_){
                double dt_ms = qpc_to_ms(stamp - last_stamp_);
                uint32_t us = (uint32_t)(dt_ms * 1000.0);
                tel.last_interval_us.store(us, std::memory_order_relaxed);
                if (us < tel.min_interval_us.load(std::memory_order_relaxed)) tel.min_interval_us.store(us, std::memory_order_relaxed);
                if (us > tel.max_interval_us.load(std::memory_order_relaxed)) tel.max_interval_us.store(us, std::memory_order_relaxed);
                hz_acc_  = hz_acc_  * 0.99 + dt_ms * 0.01;
                jit_acc_ = jit_acc_ * 0.99 + (dt_ms > hz_acc_ ? dt_ms - hz_acc_ : hz_acc_ - dt_ms) * 0.01;
                if (hz_acc_ > 0.0001) tel.hz.store((float)(1000.0 / hz_acc_), std::memory_order_relaxed);
                tel.jitter_ms.store((float)jit_acc_, std::memory_order_relaxed);
            }
            last_stamp_ = stamp;
            tel.reports.fetch_add(1, std::memory_order_relaxed);
            last_raw = rs;
            raw_seq.fetch_add(1, std::memory_order_release);
            if (eng_) eng_->on_report(this, rs, stamp);
            uint32_t proc_us = (uint32_t)(qpc_to_ms(qpc_now() - stamp) * 1000.0);
            tel.last_process_us.store(proc_us, std::memory_order_relaxed);
            if (proc_us > tel.max_process_us.load(std::memory_order_relaxed))
                tel.max_process_us.store(proc_us, std::memory_order_relaxed);
        }
        if (timer){ CancelWaitableTimer(timer); CloseHandle(timer); }
    }
    Engine* eng_ = nullptr;
    HANDLE  quit_ = nullptr;
    std::thread th_;
    uint64_t last_stamp_ = 0;
    double hz_acc_ = 0, jit_acc_ = 0;
};

// =============================================================== enumerate ==
static std::string wide_to_utf8(const wchar_t* w){
    if (!w || !*w) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::vector<std::shared_ptr<Device>> enumerate_devices(){
    std::vector<std::shared_ptr<Device>> out;

    // --- XInput slots first: reliable layout and separate trigger axes
    if (xinput_load()){
        for (int s = 0; s < 4; s++){
            XINPUT_STATE st = {};
            if (g_xget((DWORD)s, &st) != ERROR_SUCCESS) continue;
            auto d = std::make_shared<XInputDevice>();
            d->slot = s;
            d->key = "xinput:" + std::to_string(s);
            char lbl[64]; snprintf(lbl, sizeof lbl, "XInput pad %d", s + 1);
            d->label = lbl;
            d->detail = "1 ms polling, separate triggers, fixed Xbox layout";
            d->backend = BE_XINPUT;
            d->fixed_layout = true;
            d->n_raw_buttons = 15;
            d->n_raw_axes = 6;
            const char* an[6] = { "X", "Y", "Z", "Rz", "LT", "RT" };
            for (int i = 0; i < 6; i++) d->axis_usage_name[i] = an[i];
            out.push_back(d);
        }
    }

    // --- HID interfaces
    GUID hid_guid; HidD_GetHidGuid(&hid_guid);
    HDEVINFO set = SetupDiGetClassDevsA(&hid_guid, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set != INVALID_HANDLE_VALUE){
        SP_DEVICE_INTERFACE_DATA ifd = {}; ifd.cbSize = sizeof ifd;
        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hid_guid, i, &ifd); i++){
            DWORD need = 0;
            SetupDiGetDeviceInterfaceDetailA(set, &ifd, nullptr, 0, &need, nullptr);
            if (!need) continue;
            std::vector<char> blob(need);
            auto* det = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)blob.data();
            det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
            if (!SetupDiGetDeviceInterfaceDetailA(set, &ifd, det, need, nullptr, nullptr)) continue;

            auto d = std::make_shared<PadDevice>();
            if (!d->open_probe(det->DevicePath)){
                if (d->h != INVALID_HANDLE_VALUE) CloseHandle(d->h);
                if (d->parse.pp) HidD_FreePreparsedData(d->parse.pp);
                continue;
            }
            HIDD_ATTRIBUTES at = {}; at.Size = sizeof at;
            HidD_GetAttributes(d->h, &at);
            wchar_t prod[256] = {}, manu[256] = {};
            HidD_GetProductString(d->h, prod, sizeof prod);
            HidD_GetManufacturerString(d->h, manu, sizeof manu);
            std::string name = wide_to_utf8(prod);
            if (name.empty()) name = wide_to_utf8(manu);
            char idbuf[64];
            snprintf(idbuf, sizeof idbuf, "hid:%04X:%04X", at.VendorID, at.ProductID);
            if (name.empty()){ name = idbuf; }
            // make the key unique if the same VID/PID shows up twice
            std::string key = idbuf;
            int dup = 0;
            for (auto& e : out) if (e->key.rfind(key, 0) == 0) dup++;
            if (dup) key += "#" + std::to_string(dup);

            d->key   = key;
            d->label = name;
            char det2[192];
            snprintf(det2, sizeof det2, "%d buttons, %d axes%s, %s",
                     d->n_raw_buttons, d->n_raw_axes, d->has_hat ? " + hat" : "",
                     d->read_ok ? "direct read" : "Raw Input only");
            d->detail = det2;
            if (at.VendorID == 0x045E && (at.ProductID == 0x028E || at.ProductID == 0x028F))
                d->detail += " - XInput shim, both triggers share one axis";
            out.push_back(d);
        }
        SetupDiDestroyDeviceInfoList(set);
    }
    return out;
}

// ------------------------------------------- default hardware binding guess --
void device_autobind(Device* dev, DeviceProfile* dp){
    for (int i = 0; i < C_BTN_COUNT; i++) dp->btn[i] = HwBind();
    for (int a = 0; a < AX_COUNT; a++)    dp->axis[a] = HwBind();

    if (dev->fixed_layout){
        for (int i = 0; i < C_BTN_COUNT && i < 15; i++){ dp->btn[i].kind = HW_BUTTON; dp->btn[i].index = i; }
        for (int a = 0; a < AX_COUNT; a++){ dp->axis[a].kind = HW_AXIS; dp->axis[a].index = a; }
        return;
    }
    // Buttons: HID gamepads report them in roughly Xbox order.
    const int order[] = { C_A, C_B, C_X, C_Y, C_LB, C_RB, C_BACK, C_START,
                          C_LS, C_RS, C_GUIDE, C_L4, C_R4, C_L5, C_R5 };
    int nb = dev->n_raw_buttons;
    for (int i = 0; i < (int)(sizeof order / sizeof order[0]) && i < nb; i++){
        dp->btn[order[i]].kind = HW_BUTTON;
        dp->btn[order[i]].index = i;
    }
    // D-pad from the hat switch when the device has one.
    if (dev->has_hat){
        dp->btn[C_DU].kind = HW_HAT; dp->btn[C_DU].index = 0;
        dp->btn[C_DR].kind = HW_HAT; dp->btn[C_DR].index = 2;
        dp->btn[C_DD].kind = HW_HAT; dp->btn[C_DD].index = 4;
        dp->btn[C_DL].kind = HW_HAT; dp->btn[C_DL].index = 6;
    }
    // Axes by HID usage: X/Y left stick, Z/Rz right stick, Rx/Ry triggers.
    auto find_usage = [&](const char* want)->int{
        for (int i = 0; i < dev->n_raw_axes; i++) if (dev->axis_usage_name[i] == want) return i;
        return -1;
    };
    struct { int logical; const char* first; const char* second; } guess[] = {
        { AX_LX, "X",  nullptr },
        { AX_LY, "Y",  nullptr },
        { AX_RX, "Z",  "Rx" },
        { AX_RY, "Rz", "Ry" },
        { AX_LT, "Rx", "Slider" },
        { AX_RT, "Ry", "Dial" },
    };
    bool used[16] = {};
    for (auto& g : guess){
        int ix = find_usage(g.first);
        if (ix < 0 || used[ix]) ix = g.second ? find_usage(g.second) : -1;
        if (ix < 0 || used[ix]) continue;
        used[ix] = true;
        dp->axis[g.logical].kind = HW_AXIS;
        dp->axis[g.logical].index = ix;
    }
    // If Rx/Ry were taken by the right stick there is nothing left for triggers;
    // that is normal on pads whose triggers are digital buttons.
}
