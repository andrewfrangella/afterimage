CXX ?= c++
OPENCV_PC := $(shell pkg-config --exists opencv4 && echo opencv4 || echo opencv5)
CPPFLAGS += -Isrc $(shell pkg-config --cflags Qt6Widgets $(OPENCV_PC))
CXXFLAGS += -std=c++17 -fPIC -O2 -Wall -Wextra -pthread
LDLIBS += $(shell pkg-config --libs Qt6Widgets) -lopencv_videoio -lopencv_imgproc -lopencv_core
all: build/afterimage build/engine-test build/integration-test
build:
	mkdir -p build
build/afterimage: src/main.cpp src/engine.cpp src/engine.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp src/engine.cpp -o $@ $(LDLIBS)
build/engine-test: tests/engine_test.cpp src/engine.cpp src/engine.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/engine_test.cpp src/engine.cpp -o $@ $(LDLIBS)
build/integration-test: tests/integration_test.cpp src/main.cpp src/engine.cpp src/engine.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/integration_test.cpp src/engine.cpp -o $@ $(LDLIBS)
test: all
	./build/engine-test
	QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion ./build/integration-test
	QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= ./build/afterimage --smoke-test
.PHONY: all test
