CXX ?= c++
OPENCV_PC := $(shell pkg-config --exists opencv4 && echo opencv4 || echo opencv5)
CPPFLAGS += -Isrc $(shell pkg-config --cflags Qt6Widgets Qt6OpenGL Qt6Network Qt6Multimedia $(OPENCV_PC))
CXXFLAGS += -std=c++17 -fPIC -O2 -Wall -Wextra -pthread
LDLIBS += $(shell pkg-config --libs Qt6Widgets Qt6OpenGL Qt6Network Qt6Multimedia) -lopencv_videoio -lopencv_imgproc -lopencv_core
COMMON := src/engine.cpp src/gpu_engine.cpp src/modulation.cpp src/syphon_output_stub.cpp
HEADERS := $(wildcard src/*.hpp)
all: build/afterimage build/engine-test build/integration-test build/gpu-test build/modulation-test
build:
	mkdir -p build
build/afterimage: src/main.cpp $(COMMON) $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp $(COMMON) -o $@ $(LDLIBS)
build/engine-test: tests/engine_test.cpp src/engine.cpp src/engine.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/engine_test.cpp src/engine.cpp -o $@ $(LDLIBS)
build/integration-test: tests/integration_test.cpp src/main.cpp $(COMMON) $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/integration_test.cpp $(COMMON) -o $@ $(LDLIBS)
build/gpu-test: tests/gpu_test.cpp src/gpu_engine.cpp src/engine.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/gpu_test.cpp src/gpu_engine.cpp src/engine.cpp -o $@ $(LDLIBS)
build/modulation-test: tests/modulation_test.cpp src/modulation.cpp src/engine.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/modulation_test.cpp src/modulation.cpp src/engine.cpp -o $@ $(LDLIBS)
test: all
	./build/engine-test
	QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion ./build/modulation-test
	QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion ./build/integration-test
	QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= ./build/afterimage --smoke-test
.PHONY: all test
