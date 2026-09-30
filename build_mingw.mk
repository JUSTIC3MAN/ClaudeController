# Cross-build from Linux, or native with MinGW on Windows.
#   make            -> build/ClaudeController.exe
CXX      = x86_64-w64-mingw32-g++
WINDRES  = x86_64-w64-mingw32-windres
IMGUI    := third_party/imgui
CXXFLAGS := -std=c++17 -O2 -ffast-math -Wall -Wno-unused-parameter \
            -Isrc -I$(IMGUI) -I$(IMGUI)/backends \
            -DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD -DIMGUI_IMPL_WIN32_DISABLE_LINKING_XINPUT \
            -DIMGUI_DISABLE_DEMO_WINDOWS -DNDEBUG
LDFLAGS  := -static -mwindows
LIBS     := -lopengl32 -lgdi32 -luser32 -lshell32 -ldwmapi -limm32 -lwinmm \
            -lhid -lsetupapi -lcomdlg32 -lole32
SRC := src/main.cpp src/gui.cpp src/engine.cpp src/input.cpp src/outputs.cpp src/mapping.cpp \
       $(IMGUI)/imgui.cpp $(IMGUI)/imgui_draw.cpp $(IMGUI)/imgui_tables.cpp $(IMGUI)/imgui_widgets.cpp \
       $(IMGUI)/backends/imgui_impl_win32.cpp $(IMGUI)/backends/imgui_impl_opengl3.cpp
OUT := build/ClaudeController.exe

all: $(OUT)

build/app.res: app.rc app.manifest icon.ico
	@mkdir -p build
	$(WINDRES) app.rc -O coff -o $@

$(OUT): $(SRC) src/app.h build/app.res
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRC) build/app.res -o $@ $(LDFLAGS) $(LIBS)
	@echo "built $@"

clean:
	rm -rf build
