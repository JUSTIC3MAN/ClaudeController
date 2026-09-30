#include "app.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl3.h"
#include <GL/gl.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static HGLRC g_gl = nullptr;
static HDC   g_dc = nullptr;
static bool  g_quit = false;
static bool  g_minimised = false;

typedef BOOL (WINAPI *PFN_wglSwapIntervalEXT)(int);

static bool gl_create(HWND hwnd){
    g_dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    int pf = ChoosePixelFormat(g_dc, &pfd);
    if (!pf) return false;
    SetPixelFormat(g_dc, pf, &pfd);
    g_gl = wglCreateContext(g_dc);
    if (!g_gl) return false;
    wglMakeCurrent(g_dc, g_gl);
    auto swap = (PFN_wglSwapIntervalEXT)wglGetProcAddress("wglSwapIntervalEXT");
    if (swap) swap(1);            // vsync: the GUI has no reason to run hotter
    return true;
}
static void gl_destroy(HWND hwnd){
    wglMakeCurrent(nullptr, nullptr);
    if (g_gl) wglDeleteContext(g_gl);
    if (g_dc) ReleaseDC(hwnd, g_dc);
}

static LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg){
    case WM_SIZE:
        g_minimised = (wp == SIZE_MINIMIZED);
        return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
    case WM_KEYUP:   case WM_SYSKEYUP: {
        uint16_t sc = (uint16_t)((lp >> 16) & 0xFF);
        bool ext = (lp >> 24) & 1;
        bool down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        gui_on_key(sc, ext, down, (uint32_t)wp);
        break;
    }
    case WM_CLOSE:
        g_quit = true;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main(int, char**){
    // 1 ms timer resolution and a bit of scheduler headroom: both matter for the
    // polling thread's cadence. Device threads run TIME_CRITICAL inside this.
    timeBeginPeriod(1);
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof wc;
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, L"APPICON");
    wc.lpszClassName = L"ClaudeControllerWnd";
    RegisterClassExW(&wc);

    float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0,0}, MONITOR_DEFAULTTOPRIMARY));
    if (dpi <= 0.f) dpi = 1.f;
    int win_w = (int)(1340 * dpi), win_h = (int)(880 * dpi);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"ClaudeController - gamepad to MIDI and keyboard",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, win_w, win_h,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return 1;
    if (!gl_create(hwnd)){ MessageBoxW(nullptr, L"Could not create an OpenGL context.", L"ClaudeController", MB_ICONERROR); return 1; }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplOpenGL3_Init("#version 130");

    g_engine.init();
    gui_init();
    ImGui::GetStyle().FontScaleDpi = dpi;

    while (!g_quit && !gui_wants_quit()){
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)){
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) g_quit = true;
        }
        if (g_quit) break;
        if (g_minimised){ Sleep(40); continue; }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        gui_frame();
        ImGui::Render();

        RECT rc; GetClientRect(hwnd, &rc);
        glViewport(0, 0, rc.right - rc.left, rc.bottom - rc.top);
        glClearColor(0.055f, 0.067f, 0.086f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(g_dc);
        if (GetForegroundWindow() != hwnd) Sleep(12);    // idle politely in the background
    }

    gui_shutdown();
    g_engine.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    gl_destroy(hwnd);
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    timeEndPeriod(1);
    return 0;
}
