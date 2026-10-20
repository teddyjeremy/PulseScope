CXX ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -O2 -Iinclude -Itests -pthread
LDFLAGS ?= -pthread

CORE := \
  src/core/timestamp.cpp \
  src/core/json.cpp \
  src/core/identity.cpp \
  src/core/session.cpp \
  src/core/schedule.cpp \
  src/core/cpu.cpp \
  src/core/memory.cpp \
  src/core/compare.cpp \
  src/core/filter.cpp \
  src/core/timeline.cpp \
  src/core/export.cpp \
  src/core/bookmarks.cpp \
  src/core/fixture.cpp \
  src/core/validate.cpp \
  src/core/settings.cpp \
  src/core/log.cpp

.PHONY: all test cli clean

all: build/pulsescope-cli build/pulsescope-tests

build/pulsescope-cli: $(CORE) src/cli/main.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(CORE) src/cli/main.cpp $(LDFLAGS)

build/pulsescope-tests: $(CORE) tests/engine_tests.cpp tests/test_main.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(CORE) tests/engine_tests.cpp tests/test_main.cpp $(LDFLAGS)

test: build/pulsescope-tests
	./build/pulsescope-tests

cli: build/pulsescope-cli

clean:
	rm -rf build
