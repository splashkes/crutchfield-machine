CXX      ?= g++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra
LDLIBS   ?= -lglfw -lGLEW -lGL

SRCS = main.cpp camera.cpp
OBJS = $(SRCS:.cpp=.o)
BIN  = feedback

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean
