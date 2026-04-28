ROOT      := ..
CXX       ?= g++
CC        ?= gcc
PYTHON    ?= python3

CXXFLAGS  ?= -O3 -std=c++17 -Wall -Wextra \
             -Wno-missing-field-initializers -Wno-unused-function \
             -DGLEW_STATIC -I$(ROOT)/vendor/quickjs

# QuickJS is pure C99. Its opcode-dispatch / parser patterns trip -Wextra
# warnings that are false positives — suppress the same set the Windows
# build uses so noise stays zero.
QJSFLAGS  ?= -O3 -std=c99 -D_GNU_SOURCE -DCONFIG_VERSION="\"2025-04-26\"" \
             -Wno-sign-compare -Wno-unused-parameter -Wno-implicit-fallthrough \
             -Wno-array-bounds -Wno-format-truncation

LDFLAGS   ?=
# Linux link deps:
#   GL stack:   glfw + GLEW + GL
#   Threads:    pthread (miniaudio + recorder + QuickJS atomics)
#   Compress:   z (EXR writer)
#   miniaudio:  dl (dlopen for PulseAudio / JACK), m (libm), rt (clock_gettime on older glibc)
#   ALSA:       asound (miniaudio's default Linux backend)
LDLIBS    ?= -lglfw -lGLEW -lGL -lpthread -lz -ldl -lm -lrt -lasound

BUILD_DIR := build
GEN_DIR   := $(BUILD_DIR)/generated
RES_DIR   := $(BUILD_DIR)/resources
RES_STAMP := $(RES_DIR)/.prepared
BIN_DIR   := $(BUILD_DIR)/bin
QJS_DIR   := $(BUILD_DIR)/quickjs
BIN       := $(BIN_DIR)/feedback

DIST_NAME := feedback-linux-x64
DIST_DIR  := $(DIST_NAME)
DIST_TGZ  := $(DIST_NAME).tar.gz

OBJS := $(BUILD_DIR)/main.o \
        $(BUILD_DIR)/input.o \
        $(BUILD_DIR)/recorder.o \
        $(BUILD_DIR)/overlay.o \
        $(BUILD_DIR)/camera_v4l2.o \
        $(BUILD_DIR)/audio.o \
        $(BUILD_DIR)/music.o

QJS_OBJS := $(QJS_DIR)/quickjs.o \
            $(QJS_DIR)/libregexp.o \
            $(QJS_DIR)/libunicode.o \
            $(QJS_DIR)/dtoa.o

all: $(BIN) $(RES_STAMP)

$(BUILD_DIR) $(GEN_DIR) $(BIN_DIR) $(QJS_DIR):
	mkdir -p $@

$(GEN_DIR)/main.cpp $(GEN_DIR)/camera.h $(GEN_DIR)/overlay.cpp: scripts/prepare_sources.py $(ROOT)/main.cpp $(ROOT)/camera.h $(ROOT)/overlay.cpp | $(GEN_DIR)
	$(PYTHON) scripts/prepare_sources.py $(ROOT) $(GEN_DIR)

$(RES_STAMP): scripts/prepare_resources.py $(ROOT)/shaders/main.vert $(ROOT)/shaders/main.frag $(ROOT)/shaders/blit.frag $(ROOT)/presets/01_default.ini $(ROOT)/js/engine.js | $(BUILD_DIR)
	$(PYTHON) scripts/prepare_resources.py $(ROOT) $(RES_DIR)
	touch $(RES_STAMP)

$(BUILD_DIR)/main.o: $(GEN_DIR)/main.cpp $(GEN_DIR)/camera.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(GEN_DIR) -I$(ROOT) -c -o $@ $(GEN_DIR)/main.cpp

$(BUILD_DIR)/input.o: $(ROOT)/input.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/recorder.o: $(ROOT)/recorder.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/overlay.o: $(GEN_DIR)/overlay.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(ROOT) -c -o $@ $(GEN_DIR)/overlay.cpp

$(BUILD_DIR)/camera_v4l2.o: camera_v4l2.cpp $(GEN_DIR)/camera.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(GEN_DIR) -I$(ROOT) -c -o $@ $<

$(BUILD_DIR)/audio.o: $(ROOT)/audio.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -Wno-stringop-overflow -c -o $@ $<

$(BUILD_DIR)/music.o: $(ROOT)/music.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(QJS_DIR)/%.o: $(ROOT)/vendor/quickjs/%.c | $(QJS_DIR)
	$(CC) $(QJSFLAGS) -I$(ROOT)/vendor/quickjs -c -o $@ $<

$(BIN): $(OBJS) $(QJS_OBJS) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

dist: all
	rm -rf $(DIST_DIR) $(DIST_TGZ)
	mkdir -p $(DIST_DIR)
	cp $(BIN) $(DIST_DIR)/feedback
	cp -R $(RES_DIR)/shaders $(DIST_DIR)/shaders
	cp -R $(RES_DIR)/presets $(DIST_DIR)/presets
	cp -R $(ROOT)/js        $(DIST_DIR)/js
	cp -R $(ROOT)/music     $(DIST_DIR)/music
	cp README.md $(DIST_DIR)/
	cp $(ROOT)/LICENSE $(DIST_DIR)/
	cp $(ROOT)/CREDITS.md $(DIST_DIR)/
	tar -czf $(DIST_TGZ) $(DIST_DIR)

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR) $(DIST_TGZ)

.PHONY: all clean dist
