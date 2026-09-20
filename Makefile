# Thin wrapper. `make check` is the definition of green.
BUILD ?= build
GEN   ?= Ninja

.PHONY: all build check test asan fuzz clean format

all: build

build:
	cmake -S . -B $(BUILD) -G $(GEN) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD)

test:
	ctest --test-dir $(BUILD) --output-on-failure

check: build test

asan:
	cmake -S . -B $(BUILD)-asan -G $(GEN) -DCMAKE_BUILD_TYPE=Debug -DBT_SANITIZE=ON
	cmake --build $(BUILD)-asan
	ctest --test-dir $(BUILD)-asan --output-on-failure

fuzz:
	cmake -S . -B $(BUILD)-fuzz -G $(GEN) -DCMAKE_BUILD_TYPE=Debug \
		-DBT_BUILD_FUZZ=ON -DBT_BUILD_TESTS=OFF \
		-DCMAKE_C_COMPILER=clang
	cmake --build $(BUILD)-fuzz

clean:
	rm -rf $(BUILD) $(BUILD)-asan $(BUILD)-fuzz
