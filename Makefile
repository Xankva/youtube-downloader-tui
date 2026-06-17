CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -O2 -g -pthread -MMD -MP
INCLUDES := -Iinclude -Ideps/ftxui/include -Ideps/ftxui/src -Ideps
LDFLAGS := -lpthread

FTXUI_SRC := $(filter-out %_test.cpp %_fuzzer.cpp,$(wildcard deps/ftxui/src/ftxui/component/*.cpp deps/ftxui/src/ftxui/dom/*.cpp deps/ftxui/src/ftxui/screen/*.cpp))
FTXUI_OBJ := $(FTXUI_SRC:%.cpp=build/%.o)

APP_SRC := $(wildcard src/*.cpp)
APP_OBJ := $(APP_SRC:%.cpp=build/%.o)

BUILD_DIRS := $(sort $(dir $(FTXUI_OBJ) $(APP_OBJ)))

.PHONY: all clean run

all: yt-tui

$(BUILD_DIRS):
	mkdir -p $@

build/deps/ftxui/src/ftxui/%.o: deps/ftxui/src/ftxui/%.cpp | $(BUILD_DIRS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

build/src/%.o: src/%.cpp include/%.hpp | $(BUILD_DIRS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

build/src/%.o: src/%.cpp | $(BUILD_DIRS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

yt-tui: $(FTXUI_OBJ) $(APP_OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

run: yt-tui
	./yt-tui

check-deps:
	@test -f bin/yt-dlp || { echo "ERROR: bin/yt-dlp not found"; exit 1; }
	@test -f deps/json.hpp || { echo "ERROR: deps/json.hpp not found"; exit 1; }
	@test -f deps/ftxui/include/ftxui/component/component.hpp || { echo "ERROR: ftxui headers not found"; exit 1; }

clean:
	rm -rf build yt-tui

-include $(FTXUI_OBJ:.o=.d) $(APP_OBJ:.o=.d)
