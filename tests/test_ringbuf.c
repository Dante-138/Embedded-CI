/*
 * Host tests for src/ringbuf.c. The storage is always a heap block of the exact
 * size, so ASan aborts the run on any access one byte outside it.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ringbuf.h"

static int failures;

#define CHECK(cond)                                                                                          \
	do {                                                                                                 \
		if (!(cond)) {                                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);             \
			failures++;                                                                          \
		}                                                                                            \
	} while (0)

/* Deterministic pseudo-random generator (xorshift32): same sequence on every run. */
static uint32_t rng_state = 0x2468ACE1U;

static uint32_t random_u32(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

static uint8_t *new_storage(size_t size)
{
	uint8_t *storage = malloc(size);
	if (storage == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(EXIT_FAILURE);
	}
	return storage;
}

static void test_init_rejects_bad_arguments(void)
{
	ringbuf_t ring;
	uint8_t storage[4];
	CHECK(ringbuf_init(NULL, storage, sizeof storage) == -EINVAL);
	CHECK(ringbuf_init(&ring, NULL, sizeof storage) == -EINVAL);
	CHECK(ringbuf_init(&ring, storage, 0U) == -EINVAL);
	CHECK(ringbuf_init(&ring, storage, 1U) == -EINVAL); /* would hold zero bytes */
	CHECK(ringbuf_init(&ring, storage, 2U) == 0);
	CHECK(ringbuf_capacity(&ring) == 1U);
}

static void test_null_and_uninitialized_are_safe(void)
{
	ringbuf_t zeroed;
	memset(&zeroed, 0, sizeof zeroed); /* a static ring before ringbuf_init() */
	uint8_t byte = 0x5AU;
	CHECK(!ringbuf_put(NULL, 1U));
	CHECK(!ringbuf_put(&zeroed, 1U));
	CHECK(!ringbuf_get(NULL, &byte));
	CHECK(!ringbuf_get(&zeroed, &byte));
	CHECK(byte == 0x5AU);
	CHECK(ringbuf_count(NULL) == 0U && ringbuf_count(&zeroed) == 0U);
	CHECK(ringbuf_space(NULL) == 0U && ringbuf_space(&zeroed) == 0U);
	CHECK(ringbuf_capacity(NULL) == 0U);
	CHECK(ringbuf_write(&zeroed, &byte, 1U) == 0U);
	CHECK(ringbuf_read(&zeroed, &byte, 1U) == 0U);

	uint8_t storage[4];
	ringbuf_t ring;
	CHECK(ringbuf_init(&ring, storage, sizeof storage) == 0);
	CHECK(ringbuf_put(&ring, 1U));
	CHECK(!ringbuf_get(&ring, NULL)); /* the byte must not be consumed */
	CHECK(ringbuf_count(&ring) == 1U);
	CHECK(ringbuf_write(&ring, NULL, 3U) == 0U);
	CHECK(ringbuf_read(&ring, NULL, 3U) == 0U);
}

static void test_fifo_order_and_full(void)
{
	enum { SIZE = 8 };
	uint8_t *storage = new_storage(SIZE);
	ringbuf_t ring;
	CHECK(ringbuf_init(&ring, storage, SIZE) == 0);
	CHECK(ringbuf_count(&ring) == 0U);
	CHECK(ringbuf_space(&ring) == SIZE - 1U);

	for (uint8_t i = 0U; i < SIZE - 1U; i++) {
		CHECK(ringbuf_put(&ring, (uint8_t)(0x10U + i)));
	}
	CHECK(ringbuf_count(&ring) == SIZE - 1U);
	CHECK(ringbuf_space(&ring) == 0U);
	CHECK(!ringbuf_put(&ring, 0xFFU)); /* full: the new byte is dropped */

	for (uint8_t i = 0U; i < SIZE - 1U; i++) {
		uint8_t byte = 0U;
		CHECK(ringbuf_get(&ring, &byte));
		CHECK(byte == (uint8_t)(0x10U + i)); /* oldest first, 0xFF never stored */
	}
	uint8_t byte = 0xAAU;
	CHECK(!ringbuf_get(&ring, &byte));
	CHECK(byte == 0xAAU); /* untouched when empty */
	free(storage);
}

static void test_wraparound_many_times(void)
{
	enum { SIZE = 5 };
	uint8_t *storage = new_storage(SIZE);
	ringbuf_t ring;
	CHECK(ringbuf_init(&ring, storage, SIZE) == 0);
	uint8_t expected = 0U;
	uint8_t next = 0U;
	for (unsigned round = 0U; round < 1000U; round++) {
		/* 3 in, 3 out: the indices cross the end of the storage on most rounds. */
		for (unsigned i = 0U; i < 3U; i++) {
			CHECK(ringbuf_put(&ring, next));
			next++;
		}
		for (unsigned i = 0U; i < 3U; i++) {
			uint8_t byte = 0U;
			CHECK(ringbuf_get(&ring, &byte));
			CHECK(byte == expected);
			expected++;
		}
		CHECK(ringbuf_count(&ring) == 0U);
	}
	free(storage);
}

static void test_block_write_and_read(void)
{
	enum { SIZE = 8 };
	uint8_t *storage = new_storage(SIZE);
	ringbuf_t ring;
	CHECK(ringbuf_init(&ring, storage, SIZE) == 0);

	const uint8_t data[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U};
	CHECK(ringbuf_write(&ring, data, sizeof data) == SIZE - 1U); /* only 7 fit */
	CHECK(ringbuf_write(&ring, data, sizeof data) == 0U);

	uint8_t out[16];
	memset(out, 0xEE, sizeof out);
	CHECK(ringbuf_read(&ring, out, 3U) == 3U);
	CHECK(memcmp(out, data, 3U) == 0);
	CHECK(out[3] == 0xEEU); /* nothing written past what was asked */
	CHECK(ringbuf_read(&ring, out, sizeof out) == 4U);
	CHECK(memcmp(out, &data[3], 4U) == 0);
	CHECK(ringbuf_read(&ring, out, sizeof out) == 0U);
	free(storage);
}

/*
 * Random puts and gets checked against a plain reference model (an array with
 * read and write counters), for every size from 2 to 33. Any lost, duplicated
 * or reordered byte, or a wrong count, fails here.
 */
static void test_against_reference_model(void)
{
	enum { OPERATIONS = 20000 };
	static uint8_t model[OPERATIONS];

	for (size_t size = 2U; size <= 33U; size++) {
		uint8_t *storage = new_storage(size);
		ringbuf_t ring;
		CHECK(ringbuf_init(&ring, storage, size) == 0);
		size_t model_write = 0U;
		size_t model_read = 0U;

		for (unsigned n = 0U; n < (unsigned)OPERATIONS; n++) {
			const uint32_t random = random_u32();
			const size_t model_count = model_write - model_read;
			if ((random & 1U) != 0U) {
				const uint8_t byte = (uint8_t)(random >> 8);
				const bool accepted = ringbuf_put(&ring, byte);
				CHECK(accepted == (model_count < size - 1U));
				if (accepted) {
					model[model_write] = byte;
					model_write++;
				}
			} else {
				uint8_t byte = 0U;
				const bool available = ringbuf_get(&ring, &byte);
				CHECK(available == (model_count > 0U));
				if (available) {
					CHECK(byte == model[model_read]);
					model_read++;
				}
			}
			CHECK(ringbuf_count(&ring) == model_write - model_read);
			CHECK(ringbuf_count(&ring) + ringbuf_space(&ring) == size - 1U);
		}
		free(storage);
	}
}

int main(void)
{
	test_init_rejects_bad_arguments();
	test_null_and_uninitialized_are_safe();
	test_fifo_order_and_full();
	test_wraparound_many_times();
	test_block_write_and_read();
	test_against_reference_model();

	if (failures != 0) {
		fprintf(stderr, "ringbuf: %d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	printf("ringbuf: all tests passed\n");
	return EXIT_SUCCESS;
}
