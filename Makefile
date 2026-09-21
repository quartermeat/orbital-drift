CXX ?= g++
RAYLIB := build/deps/raylib-5.5/src
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wpedantic -pthread
LDLIBS := $(RAYLIB)/libraylib.a -lGL -lm -lpthread -ldl -lrt -lX11

.PHONY: all setup run test check clean figures
all: build/orbital-drift
setup:
	python3 scripts/setup.py
$(RAYLIB)/raylib.h:
	python3 scripts/setup.py
$(RAYLIB)/libraylib.a: $(RAYLIB)/raylib.h
	$(MAKE) -C $(RAYLIB) PLATFORM=PLATFORM_DESKTOP GRAPHICS=GRAPHICS_API_OPENGL_33 RAYLIB_LIBTYPE=STATIC -j4
build/orbital-drift: src/main.cpp src/mixer.hpp src/hotreload.hpp src/progress.hpp src/scene.hpp src/campaign.hpp src/figure.hpp assets/space.fs assets/layers.conf $(RAYLIB)/libraylib.a
	$(CXX) $(CXXFLAGS) -Isrc -isystem $(RAYLIB) src/main.cpp -o $@ $(LDLIBS)
build/mixer-test: tests/mixer_test.cpp src/mixer.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@
build/config-test: tests/config_test.cpp src/hotreload.hpp src/mixer.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@
build/progress-test: tests/progress_test.cpp src/progress.hpp src/mixer.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@
build/scene-test: tests/scene_test.cpp src/scene.hpp src/hotreload.hpp src/mixer.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@
build/campaign-test: tests/campaign_test.cpp src/campaign.hpp src/person.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@
test: build/mixer-test build/config-test build/progress-test build/scene-test build/campaign-test
	./build/mixer-test
	./build/config-test
	./build/progress-test
	./build/scene-test
	./build/campaign-test
check: test all
	./build/orbital-drift --check-assets
run: all
	./build/orbital-drift
build/figure-sheet: tools/figure_sheet.cpp src/figure.hpp src/scene.hpp $(RAYLIB)/libraylib.a
	mkdir -p build artifacts
	$(CXX) $(CXXFLAGS) -Isrc -isystem $(RAYLIB) $< -o $@ $(LDLIBS)
figures: build/figure-sheet
	./build/figure-sheet
