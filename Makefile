# Host tests for the C modules: sanitizers on, warnings as errors.
#
#   make test     build with ASan + UBSan and run the tests
#   make proof    check that the sanitizer is really on (the deliberate bug must be caught)
#   make analyze  run cppcheck (static analysis)
#   make clean    remove build/
#
# On Windows, run it inside the same container as CI (see README.md).

CC      ?= gcc
BUILD   := build
SRC     := src/frame.c
HDR     := src/frame.h

WARN    := -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Wsign-conversion \
           -Wcast-qual -Wformat=2 -Wnull-dereference -Wdouble-promotion -Wstrict-prototypes
SANIT   := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
CFLAGS  := -std=c11 -g -O1 $(WARN) $(SANIT) -Isrc

# Stop at the first violation with a full stack trace, and check for leaks.
export ASAN_OPTIONS  := abort_on_error=1:detect_leaks=1:strict_string_checks=1:detect_stack_use_after_return=1
export UBSAN_OPTIONS := print_stacktrace=1:halt_on_error=1

.PHONY: test proof analyze clean

test: $(BUILD)/test_frame
	./$(BUILD)/test_frame

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

clean:
	rm -rf $(BUILD)
