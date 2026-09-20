CXX ?= g++
RAYLIB := build/deps/raylib-5.5/src
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wpedantic -pthread
LDLIBS := $(RAYLIB)/libraylib.a -lGL -lm -lpthread -ldl -lrt -lX11

.PHONY: all setup run test check clean
all: build/orbital-drift
setup:
	python3 scripts/setup.py
$(RAYLIB)/raylib.h:
	python3 scripts/setup.py
$(RAYLIB)/libraylib.a: $(RAYLIB)/raylib.h
	$(MAKE) -C $(RAYLIB) PLATFORM=PLATFORM_DESKTOP GRAPHICS=GRAPHICS_API_OPENGL_33 RAYLIB_LIBTYPE=STATIC -j4
build/orbital-drift: src/main.cpp src/mixer.hpp src/hotreload.hpp assets/space.fs assets/layers.conf $(RAYLIB)/libraylib.a
	$(CXX) $(CXXFLAGS) -Isrc -I$(RAYLIB) src/main.cpp -o $@ $(LDLIBS)
build/mixer-test: tests/mixer_test.cpp src/mixer.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@
build/config-test: tests/config_test.cpp src/hotreload.hpp src/mixer.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@
test: build/mixer-test build/config-test
	./build/mixer-test
	./build/config-test
check: test all
	./build/orbital-drift --check-assets
run: all
	./build/orbital-drift
