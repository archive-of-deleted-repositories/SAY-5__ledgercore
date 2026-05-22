# Convenience wrappers around cmake. CI calls cmake directly.
BUILD ?= build
JOBS  ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

.PHONY: all configure build test bench asan tsan fuzz clean format

all: build

configure:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release

build: configure
	cmake --build $(BUILD) -j $(JOBS)

test: build
	ctest --test-dir $(BUILD) --output-on-failure

bench: build
	$(BUILD)/bench_harness --txns 1000000 --workers 4 --json bench/results/bench_local.json

asan:
	cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLEDGERCORE_ASAN=ON
	cmake --build build-asan -j $(JOBS)
	ctest --test-dir build-asan --output-on-failure

tsan:
	cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLEDGERCORE_TSAN=ON
	cmake --build build-tsan -j $(JOBS)
	ctest --test-dir build-tsan --output-on-failure -L concurrency

fuzz:
	cmake -S . -B build-fuzz -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLEDGERCORE_BUILD_FUZZ=ON
	cmake --build build-fuzz -j $(JOBS)

format:
	find src tests bench -name '*.h' -o -name '*.cpp' -o -name '*.cc' | xargs clang-format -i

clean:
	rm -rf build build-asan build-tsan build-fuzz
