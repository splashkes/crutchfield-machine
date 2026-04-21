ROOT        := ..
BREW_PREFIX ?= $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
CXX         ?= clang++
CC          ?= clang
PYTHON      ?= python3
ARCH        ?= arm64

CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -Wno-deprecated-declarations \
            -Wno-missing-field-initializers -DGL_SILENCE_DEPRECATION \
            -arch $(ARCH) -I$(BREW_PREFIX)/include -I$(ROOT)/vendor/quickjs
OBJCXXFLAGS ?= $(CXXFLAGS) -fobjc-arc
QJSFLAGS = -O3 -std=c99 -DCONFIG_VERSION="\"2025-04-26\"" -arch $(ARCH) \
           -Wno-sign-compare -Wno-unused-parameter -Wno-implicit-fallthrough \
           -Wno-array-bounds -Wno-format-truncation
LDFLAGS  ?= -arch $(ARCH) -L$(BREW_PREFIX)/lib
LDLIBS   ?= -lglfw -lGLEW -lz \
            -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo \
            -framework Foundation -framework AVFoundation -framework CoreMedia

BUILD_DIR     := build
GEN_DIR       := $(BUILD_DIR)/generated
RES_DIR       := $(BUILD_DIR)/resources
RES_STAMP     := $(RES_DIR)/.prepared
BIN_DIR       := $(BUILD_DIR)/bin
BIN           := $(BIN_DIR)/feedback
APP_NAME      := feedback.app
APP_DIR       := $(APP_NAME)
APP_BIN_DIR   := $(APP_DIR)/Contents/MacOS
APP_RES_DIR   := $(APP_DIR)/Contents/Resources
APP_FW_DIR    := $(APP_DIR)/Contents/Frameworks
DIST_NAME     := feedback-macos-arm64
DIST_DIR      := $(DIST_NAME)
DIST_ZIP      := $(DIST_NAME).zip

GLFW_DYLIB := $(BREW_PREFIX)/opt/glfw/lib/libglfw.3.dylib
GLEW_DYLIB := $(BREW_PREFIX)/opt/glew/lib/libGLEW.2.3.dylib

OBJS := $(BUILD_DIR)/main.o \
        $(BUILD_DIR)/input.o \
        $(BUILD_DIR)/audio.o \
        $(BUILD_DIR)/music.o \
        $(BUILD_DIR)/recorder.o \
        $(BUILD_DIR)/overlay.o \
        $(BUILD_DIR)/camera_avfoundation.o

QJS_OBJS := $(BUILD_DIR)/quickjs.o \
            $(BUILD_DIR)/libregexp.o \
            $(BUILD_DIR)/libunicode.o \
            $(BUILD_DIR)/dtoa.o

all: $(APP_NAME)

$(GEN_DIR):
	mkdir -p $(GEN_DIR) $(BIN_DIR)

$(GEN_DIR)/main.cpp $(GEN_DIR)/camera.h $(GEN_DIR)/overlay.cpp: scripts/prepare_sources.py $(ROOT)/main.cpp $(ROOT)/camera.h $(ROOT)/overlay.cpp | $(GEN_DIR)
	$(PYTHON) scripts/prepare_sources.py $(ROOT) $(GEN_DIR)

$(RES_STAMP): scripts/prepare_resources.py $(ROOT)/shaders/main.vert $(ROOT)/shaders/main.frag $(ROOT)/shaders/blit.frag $(ROOT)/presets/01_default.ini | $(GEN_DIR)
	$(PYTHON) scripts/prepare_resources.py $(ROOT) $(RES_DIR)
	touch $(RES_STAMP)

$(BUILD_DIR)/main.o: $(GEN_DIR)/main.cpp $(GEN_DIR)/camera.h | $(GEN_DIR)
	$(CXX) $(CXXFLAGS) -I$(GEN_DIR) -I$(ROOT) -c -o $@ $(GEN_DIR)/main.cpp

$(BUILD_DIR)/input.o: $(ROOT)/input.cpp | $(GEN_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/audio.o: $(ROOT)/audio.cpp | $(GEN_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/music.o: $(ROOT)/music.cpp | $(GEN_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/recorder.o: $(ROOT)/recorder.cpp | $(GEN_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/overlay.o: $(GEN_DIR)/overlay.cpp | $(GEN_DIR)
	$(CXX) $(CXXFLAGS) -I$(GEN_DIR) -I$(ROOT) -c -o $@ $(GEN_DIR)/overlay.cpp

$(BUILD_DIR)/camera_avfoundation.o: camera_avfoundation.mm $(GEN_DIR)/camera.h | $(GEN_DIR)
	$(CXX) $(OBJCXXFLAGS) -I$(GEN_DIR) -I$(ROOT) -c -o $@ $<

$(BUILD_DIR)/quickjs.o: $(ROOT)/vendor/quickjs/quickjs.c | $(GEN_DIR)
	$(CC) $(QJSFLAGS) -I$(ROOT)/vendor/quickjs -c -o $@ $<

$(BUILD_DIR)/libregexp.o: $(ROOT)/vendor/quickjs/libregexp.c | $(GEN_DIR)
	$(CC) $(QJSFLAGS) -I$(ROOT)/vendor/quickjs -c -o $@ $<

$(BUILD_DIR)/libunicode.o: $(ROOT)/vendor/quickjs/libunicode.c | $(GEN_DIR)
	$(CC) $(QJSFLAGS) -I$(ROOT)/vendor/quickjs -c -o $@ $<

$(BUILD_DIR)/dtoa.o: $(ROOT)/vendor/quickjs/dtoa.c | $(GEN_DIR)
	$(CC) $(QJSFLAGS) -I$(ROOT)/vendor/quickjs -c -o $@ $<

$(BIN): $(OBJS) $(QJS_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(APP_NAME): $(BIN) $(RES_STAMP) Info.plist
	rm -rf $(APP_DIR)
	mkdir -p $(APP_BIN_DIR) $(APP_RES_DIR) $(APP_FW_DIR)
	cp $(BIN) $(APP_BIN_DIR)/feedback
	cp Info.plist $(APP_DIR)/Contents/Info.plist
	cp -R $(RES_DIR)/* $(APP_RES_DIR)/
	cp $(GLFW_DYLIB) $(APP_FW_DIR)/
	cp $(GLEW_DYLIB) $(APP_FW_DIR)/
	install_name_tool -change $(GLFW_DYLIB) @executable_path/../Frameworks/libglfw.3.dylib $(APP_BIN_DIR)/feedback
	install_name_tool -change $(GLEW_DYLIB) @executable_path/../Frameworks/libGLEW.2.3.dylib $(APP_BIN_DIR)/feedback
	install_name_tool -id @rpath/libglfw.3.dylib $(APP_FW_DIR)/libglfw.3.dylib
	install_name_tool -id @rpath/libGLEW.2.3.dylib $(APP_FW_DIR)/libGLEW.2.3.dylib
	codesign --force --deep --sign - $(APP_DIR)

dist: $(APP_NAME)
	rm -rf $(DIST_DIR) $(DIST_ZIP)
	mkdir -p $(DIST_DIR)
	cp -R $(APP_NAME) $(DIST_DIR)/
	cp README.md $(DIST_DIR)/
	cp $(ROOT)/LICENSE $(DIST_DIR)/
	cp $(ROOT)/CREDITS.md $(DIST_DIR)/
	ditto -c -k --sequesterRsrc --keepParent $(DIST_DIR) $(DIST_ZIP)

clean:
	rm -rf $(BUILD_DIR) $(APP_DIR) $(DIST_DIR) $(DIST_ZIP)

.PHONY: all clean dist
