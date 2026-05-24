# Native Apple Silicon build isolated under macOS/.
#
# Prereqs:
#   brew install glfw glew pkg-config
#
# Then:
#   cd macOS
#   make
#   make dist
#   open feedback.app

ROOT        := ..
BREW_PREFIX ?= $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
CXX         ?= clang++
ARCH        ?= arm64

CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -Wno-deprecated-declarations \
            -Wno-missing-field-initializers -DGL_SILENCE_DEPRECATION \
            -arch $(ARCH) -I$(BREW_PREFIX)/include -I$(ROOT) \
            -I$(ROOT)/vendor/quickjs
OBJCXXFLAGS ?= $(CXXFLAGS) -fobjc-arc

# QuickJS — pure C99, suppress the noise-generating warnings the
# Windows + Linux builds also suppress.
CC       ?= clang
QJSFLAGS ?= -O3 -std=c99 -D_GNU_SOURCE -DCONFIG_VERSION="\"2025-04-26\"" \
            -arch $(ARCH) \
            -Wno-sign-compare -Wno-unused-parameter -Wno-implicit-fallthrough \
            -Wno-array-bounds -Wno-format-truncation

LDFLAGS  ?= -arch $(ARCH) -L$(BREW_PREFIX)/lib
LDLIBS   ?= -lglfw -lGLEW -lz \
            -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo \
            -framework Foundation -framework AVFoundation -framework CoreMedia \
            -framework CoreMIDI

BUILD_DIR    := build
BIN          := $(BUILD_DIR)/feedback
APP_NAME     := feedback.app
APP_DIR      := $(APP_NAME)
APP_BIN_DIR  := $(APP_DIR)/Contents/MacOS
APP_RES_DIR  := $(APP_DIR)/Contents/Resources
APP_FW_DIR   := $(APP_DIR)/Contents/Frameworks
DIST_NAME    := feedback-macos-arm64
DIST_DIR     := $(DIST_NAME)
DIST_ZIP     := $(DIST_NAME).zip

CPP_SRCS := $(ROOT)/main.cpp $(ROOT)/input.cpp $(ROOT)/recorder.cpp $(ROOT)/overlay.cpp $(ROOT)/ui_panel.cpp $(ROOT)/music.cpp $(ROOT)/audio.cpp
MM_SRCS  := camera_avfoundation.mm midi_coremidi.mm clip_player_avfoundation.mm
OBJS     := $(BUILD_DIR)/main.o \
            $(BUILD_DIR)/input.o \
            $(BUILD_DIR)/recorder.o \
            $(BUILD_DIR)/overlay.o \
            $(BUILD_DIR)/ui_panel.o \
            $(BUILD_DIR)/music.o \
            $(BUILD_DIR)/audio.o \
            $(BUILD_DIR)/clip_player_avfoundation.o \
            $(BUILD_DIR)/camera_avfoundation.o \
            $(BUILD_DIR)/midi_coremidi.o

QJS_DIR  := $(BUILD_DIR)/quickjs
QJS_OBJS := $(QJS_DIR)/quickjs.o \
            $(QJS_DIR)/libregexp.o \
            $(QJS_DIR)/libunicode.o \
            $(QJS_DIR)/dtoa.o

GLFW_DYLIB := $(BREW_PREFIX)/opt/glfw/lib/libglfw.3.dylib
GLEW_DYLIB := $(BREW_PREFIX)/opt/glew/lib/libGLEW.2.3.dylib

all: $(APP_NAME)

$(BUILD_DIR) $(QJS_DIR):
	mkdir -p $@

$(BIN): $(OBJS) $(QJS_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(QJS_DIR)/%.o: $(ROOT)/vendor/quickjs/%.c | $(QJS_DIR)
	$(CC) $(QJSFLAGS) -I$(ROOT)/vendor/quickjs -c -o $@ $<

$(BUILD_DIR)/main.o: $(ROOT)/main.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/music.o: $(ROOT)/music.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/audio.o: $(ROOT)/audio.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/clip_player_avfoundation.o: $(ROOT)/clip_player_avfoundation.mm | $(BUILD_DIR)
	$(CXX) $(OBJCXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/input.o: $(ROOT)/input.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/recorder.o: $(ROOT)/recorder.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/overlay.o: $(ROOT)/overlay.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/ui_panel.o: $(ROOT)/ui_panel.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/camera_avfoundation.o: camera_avfoundation.mm | $(BUILD_DIR)
	$(CXX) $(OBJCXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/midi_coremidi.o: midi_coremidi.mm | $(BUILD_DIR)
	$(CXX) $(OBJCXXFLAGS) -c -o $@ $<

$(APP_NAME): $(BIN) Info.plist
	rm -rf $(APP_DIR)
	mkdir -p $(APP_BIN_DIR) $(APP_RES_DIR) $(APP_FW_DIR)
	cp $(BIN) $(APP_BIN_DIR)/feedback
	cp Info.plist $(APP_DIR)/Contents/Info.plist
	cp -R $(ROOT)/shaders $(APP_RES_DIR)/
	cp -R $(ROOT)/presets $(APP_RES_DIR)/
	cp -R $(ROOT)/js $(APP_RES_DIR)/
	cp -R $(ROOT)/music $(APP_RES_DIR)/
	cp $(ROOT)/ui.yaml $(APP_RES_DIR)/
	cp $(GLFW_DYLIB) $(APP_FW_DIR)/
	cp $(GLEW_DYLIB) $(APP_FW_DIR)/
	install_name_tool -change $(GLFW_DYLIB) @executable_path/../Frameworks/libglfw.3.dylib $(APP_BIN_DIR)/feedback
	install_name_tool -change $(GLEW_DYLIB) @executable_path/../Frameworks/libGLEW.2.3.dylib $(APP_BIN_DIR)/feedback
	install_name_tool -id @rpath/libglfw.3.dylib $(APP_FW_DIR)/libglfw.3.dylib
	install_name_tool -id @rpath/libGLEW.2.3.dylib $(APP_FW_DIR)/libGLEW.2.3.dylib
	codesign --force --deep --sign - $(APP_NAME)
	mkdir -p clips

dist: $(APP_NAME)
	rm -rf $(DIST_DIR) $(DIST_ZIP)
	mkdir -p $(DIST_DIR)
	cp -R $(APP_NAME) $(DIST_DIR)/
	cp README.md $(DIST_DIR)/
	cp MIDI_NOTES.md $(DIST_DIR)/
	cp $(ROOT)/LICENSE $(DIST_DIR)/
	cp $(ROOT)/CREDITS.md $(DIST_DIR)/
	ditto -c -k --sequesterRsrc --keepParent $(DIST_DIR) $(DIST_ZIP)

clean:
	rm -rf $(BUILD_DIR) $(APP_DIR) $(DIST_DIR) $(DIST_ZIP)

.PHONY: all dist clean
