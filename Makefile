# Makefile for MSYS2 / MinGW-w64 on Windows.
# In an MSYS2 MINGW64 shell:
#   pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-glfw mingw-w64-x86_64-glew
#   make
#   ./feedback.exe
#
# Run from this directory so the shader paths resolve (or put shaders/ next
# to feedback.exe — main.cpp also searches next to the exe).

CXX      ?= g++
# Compile-time flags. `-pthread` here just sets _REENTRANT for the headers;
# it does NOT appear on the link line (that would pull dynamic winpthread).
# `-Wno-missing-field-initializers` silences stb_image_write.h's `{ 0 }` inits.
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -Wno-missing-field-initializers \
            -pthread -DGLEW_STATIC

# Link-time flags. Kept SEPARATE from CXXFLAGS so `-pthread` doesn't get
# expanded to `-lpthread` on the link line.
#
# `-static` is load-bearing on MinGW: without it, libstdc++'s internal use of
# std::thread drags libwinpthread-1.dll into the import table even when we
# explicitly -Wl,-Bstatic -lwinpthread. With `-static` + selective -Bdynamic
# blocks below, MSYS2 runtime libs go into the exe and only Windows system
# DLLs remain as imports.
LDFLAGS   = -static -static-libgcc -static-libstdc++

# Static-link everything MSYS2-provided so the resulting .exe runs on any
# Windows box with no extra DLLs. System libs (opengl32, gdi32, mf*, winmm)
# stay dynamic — they're part of Windows itself.
#
# The trailing `-Wl,-Bstatic` is load-bearing: g++'s driver auto-appends
# -lstdc++ -lgcc_s at the end of the link line, and without this they'd
# inherit the -Bdynamic state and pull in libstdc++-6.dll.
LDLIBS    = -Wl,-Bstatic -lglfw3 -lglew32 -lz -lwinpthread \
            -Wl,-Bdynamic -lopengl32 -lgdi32 \
            -lmfplat -lmfreadwrite -lmf -lmfuuid -lole32 -loleaut32 \
            -lwinmm \
            -Wl,-Bstatic

SRCS = main.cpp camera.cpp recorder.cpp overlay.cpp input.cpp
OBJS = $(SRCS:.cpp=.o)
BIN  = feedback.exe

# Bundle zipped for distribution — what you upload to GitHub Releases.
DIST_NAME = feedback-windows-x64
DIST_DIR  = $(DIST_NAME)
DIST_ZIP  = $(DIST_NAME).zip

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# `make dist` produces a zip with the exe + everything it needs at runtime.
# Recipients unzip, double-click feedback.exe, done.
#
# Optional: if loopMIDI.exe is dropped into the repo root before building,
# it's bundled into the zip so Music-mode can launch it zero-install. The
# binary is freeware (https://www.tobias-erichsen.de/software/loopmidi.html)
# but confirm its EULA before redistributing.
dist: $(BIN)
	rm -rf $(DIST_DIR) $(DIST_ZIP)
	mkdir -p $(DIST_DIR)
	cp $(BIN) $(DIST_DIR)/
	cp -r shaders $(DIST_DIR)/
	cp -r presets $(DIST_DIR)/
	cp README.md LICENSE CREDITS.md $(DIST_DIR)/
	@if [ -f loopMIDI.exe ]; then \
	  echo "bundling loopMIDI.exe"; \
	  cp loopMIDI.exe $(DIST_DIR)/; \
	else \
	  echo "note: loopMIDI.exe not present — Music mode will prompt user to install"; \
	fi
	@echo "--- DLL check (should list only Windows system DLLs) ---"
	@objdump -p $(BIN) | grep "DLL Name:" || true
	@# PowerShell's Compress-Archive ships with Windows 10+, so this works
	@# in MSYS2 without needing `pacman -S zip`.
	powershell.exe -NoProfile -Command "Compress-Archive -Force -Path '$(DIST_DIR)' -DestinationPath '$(DIST_ZIP)'"
	@echo
	@echo "Built $(DIST_ZIP) — upload this to GitHub Releases."

clean:
	rm -f $(OBJS) $(BIN)
	rm -rf $(DIST_DIR) $(DIST_ZIP)

.PHONY: all dist clean
