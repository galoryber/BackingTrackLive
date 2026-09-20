# Thin wrapper. `make check` is the definition of green.
BUILD ?= build
GEN   ?= Ninja

.PHONY: all build check test asan tsan cov device fuzz clean format

# The test suite links no device code, so the default build does not fetch or
# compile PortAudio. `make device` is the one that does.
DEVICE ?= OFF

all: build

build:
	cmake -S . -B $(BUILD) -G $(GEN) -DCMAKE_BUILD_TYPE=Debug -DBT_WITH_DEVICE=$(DEVICE)
	cmake --build $(BUILD)

test:
	ctest --test-dir $(BUILD) --output-on-failure

check: build test

# TSan needs a low-entropy address space; this container's default ASLR
# settings make it refuse to start, hence setarch -R.
tsan:
	cmake -S . -B $(BUILD)-tsan -G $(GEN) -DCMAKE_BUILD_TYPE=Debug -DBT_TSAN=ON -DBT_WITH_DEVICE=OFF
	cmake --build $(BUILD)-tsan
	TSAN_OPTIONS=halt_on_error=1 setarch $$(uname -m) -R ctest --test-dir $(BUILD)-tsan --output-on-failure

device:
	cmake -S . -B $(BUILD)-dev -G $(GEN) -DCMAKE_BUILD_TYPE=Debug -DBT_WITH_DEVICE=ON
	cmake --build $(BUILD)-dev
	./$(BUILD)-dev/btplay --list-devices

asan:
	cmake -S . -B $(BUILD)-asan -G $(GEN) -DCMAKE_BUILD_TYPE=Debug -DBT_SANITIZE=ON -DBT_WITH_DEVICE=OFF
	cmake --build $(BUILD)-asan
	ctest --test-dir $(BUILD)-asan --output-on-failure

cov:
	cmake -S . -B $(BUILD)-cov -G $(GEN) -DCMAKE_BUILD_TYPE=Debug -DBT_WITH_DEVICE=OFF \
		-DCMAKE_C_FLAGS="--coverage -O0" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
	cmake --build $(BUILD)-cov
	ctest --test-dir $(BUILD)-cov --output-on-failure
	gcovr --root . --filter 'src/' --exclude 'third_party/' --print-summary --txt

fuzz:
	cmake -S . -B $(BUILD)-fuzz -G $(GEN) -DCMAKE_BUILD_TYPE=Debug \
		-DBT_BUILD_FUZZ=ON -DBT_BUILD_TESTS=OFF \
		-DCMAKE_C_COMPILER=clang
	cmake --build $(BUILD)-fuzz

clean:
	rm -rf $(BUILD) $(BUILD)-asan $(BUILD)-tsan $(BUILD)-fuzz $(BUILD)-cov $(BUILD)-dev
