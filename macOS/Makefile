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
            -arch $(ARCH) -I$(BREW_PREFIX)/include -I$(ROOT)
OBJCXXFLAGS ?= $(CXXFLAGS) -fobjc-arc
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

CPP_SRCS := main.cpp $(ROOT)/input.cpp $(ROOT)/recorder.cpp $(ROOT)/overlay.cpp
MM_SRCS  := camera_avfoundation.mm midi_coremidi.mm
OBJS     := $(BUILD_DIR)/main.o \
            $(BUILD_DIR)/input.o \
            $(BUILD_DIR)/recorder.o \
            $(BUILD_DIR)/overlay.o \
            $(BUILD_DIR)/camera_avfoundation.o \
            $(BUILD_DIR)/midi_coremidi.o

GLFW_DYLIB := $(BREW_PREFIX)/opt/glfw/lib/libglfw.3.dylib
GLEW_DYLIB := $(BREW_PREFIX)/opt/glew/lib/libGLEW.2.3.dylib

all: $(APP_NAME)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/main.o: main.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/input.o: $(ROOT)/input.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/recorder.o: $(ROOT)/recorder.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/overlay.o: $(ROOT)/overlay.cpp | $(BUILD_DIR)
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
	cp $(GLFW_DYLIB) $(APP_FW_DIR)/
	cp $(GLEW_DYLIB) $(APP_FW_DIR)/
	install_name_tool -change $(GLFW_DYLIB) @executable_path/../Frameworks/libglfw.3.dylib $(APP_BIN_DIR)/feedback
	install_name_tool -change $(GLEW_DYLIB) @executable_path/../Frameworks/libGLEW.2.3.dylib $(APP_BIN_DIR)/feedback
	install_name_tool -id @rpath/libglfw.3.dylib $(APP_FW_DIR)/libglfw.3.dylib
	install_name_tool -id @rpath/libGLEW.2.3.dylib $(APP_FW_DIR)/libGLEW.2.3.dylib
	codesign --force --deep --sign - $(APP_NAME)

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
