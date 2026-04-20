# Makefile for MSYS2 / MinGW-w64 on Windows.
# In an MSYS2 MINGW64 shell:
#   pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-glfw mingw-w64-x86_64-glew
#   make
#   ./feedback.exe
#
# Run from this directory so the shader paths resolve (or put shaders/ next
# to feedback.exe — main.cpp also searches next to the exe).

CXX      ?= g++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -pthread
LDLIBS    = -lglfw3 -lglew32 -lopengl32 -lgdi32 \
            -lmfplat -lmfreadwrite -lmf -lmfuuid -lole32 -loleaut32 \
            -lwinmm -lz \
            -static-libgcc -static-libstdc++ -pthread

SRCS = main.cpp camera.cpp recorder.cpp overlay.cpp input.cpp
OBJS = $(SRCS:.cpp=.o)
BIN  = feedback.exe

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	del /Q $(OBJS) $(BIN) 2>nul || rm -f $(OBJS) $(BIN)

.PHONY: all clean
