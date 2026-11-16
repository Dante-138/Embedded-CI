# Host tests for the C modules: sanitizers on, warnings as errors.
#
#   make test     build with ASan + UBSan and run the tests once per payload limit
#   make proof    check that each sanitizer is really on (its deliberate bug must be caught)
#   make analyze  run cppcheck (static analysis)
#   make format   rewrite the sources with clang-format (.clang-format)
#   make format-check  fail if a source is not formatted
#   make clean    remove build/
#
# On Windows, run it inside the same container as CI (see README.md).

CC      ?= gcc
BUILD   := build
SRC     := src/frame.c
HDR     := src/frame.h

WARN    := -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Wsign-conversion \
           -Wcast-qual -Wformat=2 -Wnull-dereference -Wdouble-promotion -Wstrict-prototypes \
           -Wmissing-prototypes -Wundef -Wcast-align -Wfloat-equal -Wvla -fno-common
SANIT   := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
CFLAGS  := -std=c11 -g -O1 $(WARN) $(SANIT) -Isrc

# Stop at the first violation with a full stack trace, and check for leaks.
export ASAN_OPTIONS  := abort_on_error=1:detect_leaks=1:strict_string_checks=1:detect_stack_use_after_return=1
export UBSAN_OPTIONS := print_stacktrace=1:halt_on_error=1

# Payload limits the tests run with: the default, the first value where SOF
# (0xA5) is also a valid LEN, and the largest LEN a byte can hold.
LIMITS  := 64 165 255

.PHONY: test proof analyze format format-check clean

# Every other module is one src/<name>.c + src/<name>.h + tests/test_<name>.c
# and is picked up here by itself: adding a module needs no change to this file.
MODULES := $(filter-out frame,$(patsubst src/%.c,%,$(wildcard src/*.c)))

test: $(foreach limit,$(LIMITS),$(BUILD)/max$(limit)/test_frame)       $(foreach module,$(MODULES),$(BUILD)/modules/test_$(module))
	@set -e; for limit in $(LIMITS); do ./$(BUILD)/max$$limit/test_frame; done
	@set -e; for module in $(MODULES); do ./$(BUILD)/modules/test_$$module; done

$(BUILD)/modules/test_%: tests/test_%.c src/%.c src/%.h
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $< src/$*.c -o $@

# One build folder per limit, so each binary is built with its own -D.
$(BUILD)/max%/test_frame: tests/test_frame.c $(SRC) $(HDR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -DFRAME_MAX_PAYLOAD=$*U $< $(SRC) -o $@

# Each proof program has one deliberate bug that only one sanitizer catches.
# It must crash, and with that sanitizer's own report.
define run_proof
	@if ./$(BUILD)/$(1) 2> $(BUILD)/$(1).log; then \
		echo "ERROR: the deliberate bug in $(1) passed; $(2) is off"; exit 1; \
	elif grep -q "$(3)" $(BUILD)/$(1).log; then \
		echo "ok: $(2) caught the deliberate bug in $(1)"; \
	else \
		cat $(BUILD)/$(1).log; echo "ERROR: $(1) failed, but not for the expected reason"; exit 1; \
	fi
endef

proof: $(BUILD)/proof_asan $(BUILD)/proof_ubsan
	$(call run_proof,proof_asan,AddressSanitizer,heap-buffer-overflow)
	$(call run_proof,proof_ubsan,UndefinedBehaviorSanitizer,signed integer overflow)

analyze:
	cppcheck --enable=warning,style,performance,portability --check-level=exhaustive \
		--error-exitcode=1 -q -Isrc src tests

$(BUILD)/%: tests/%.c $(SRC) $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) $< $(SRC) -o $@

$(BUILD):
	mkdir -p $(BUILD)

format:
	clang-format -i src/*.c src/*.h tests/*.c

format-check:
	clang-format --dry-run --Werror src/*.c src/*.h tests/*.c

clean:
	rm -rf $(BUILD)
