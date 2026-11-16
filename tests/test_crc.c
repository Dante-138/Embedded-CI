/*
 * Host tests for src/crc.c: published check values, independent bitwise
 * references written here from the catalogue parameters, chaining and the
 * error-detection properties a CRC must have.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc.h"

static int failures;

#define CHECK(cond)                                                                                          \
	do {                                                                                                 \
		if (!(cond)) {                                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);             \
			failures++;                                                                          \
		}                                                                                            \
	} while (0)

static const uint8_t CHECK_STRING[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

/* Deterministic pseudo-random generator (xorshift32): same bytes on every run. */
static uint32_t rng_state = 0x0BADC0DEU;

static uint8_t random_byte(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return (uint8_t)(rng_state & 0xFFU);
}

/* Reverses the lowest `width` bits of value. */
static uint32_t reflect(uint32_t value, unsigned width)
{
	uint32_t result = 0U;
	for (unsigned i = 0U; i < width; i++) {
		if ((value & ((uint32_t)1U << i)) != 0U) {
			result |= (uint32_t)1U << (width - 1U - i);
		}
	}
	return result;
}

/*
 * Reference CRC straight from the catalogue definition (width, poly, init,
 * refin = refout, xorout), one bit at a time and never reflected internally.
 * Slow and obvious on purpose: it shares no code and no table with src/crc.c.
 */
static uint32_t reference_crc(unsigned width, uint32_t poly, uint32_t init, bool reflected, uint32_t xorout,
			      const uint8_t *data, size_t length)
{
	const uint32_t top = (uint32_t)1U << (width - 1U);
	const uint32_t mask = (width == 32U) ? 0xFFFFFFFFU : (((uint32_t)1U << width) - 1U);
	uint32_t crc = init;
	for (size_t i = 0U; i < length; i++) {
		const uint32_t byte = reflected ? reflect(data[i], 8U) : data[i];
		crc ^= byte << (width - 8U);
		for (unsigned bit = 0U; bit < 8U; bit++) {
			crc = ((crc & top) != 0U) ? (((crc << 1) ^ poly) & mask) : ((crc << 1) & mask);
		}
	}
	if (reflected) {
		crc = reflect(crc, width);
	}
	return (crc ^ xorout) & mask;
}

static void test_published_check_values(void)
{
	CHECK(crc16_ccitt_false(CRC16_INIT, CHECK_STRING, sizeof CHECK_STRING) == 0x29B1U);
	CHECK(crc16_modbus(CRC16_INIT, CHECK_STRING, sizeof CHECK_STRING) == 0x4B37U);
	CHECK(crc32_ieee(CRC32_INIT, CHECK_STRING, sizeof CHECK_STRING) == 0xCBF43926UL);
}

static void test_reference_agrees_with_check_values(void)
{
	/* The reference itself must be right before it is used to judge the module. */
	CHECK(reference_crc(16U, 0x1021U, 0xFFFFU, false, 0U, CHECK_STRING, sizeof CHECK_STRING) == 0x29B1U);
	CHECK(reference_crc(16U, 0x8005U, 0xFFFFU, true, 0U, CHECK_STRING, sizeof CHECK_STRING) == 0x4B37U);
	CHECK(reference_crc(32U, 0x04C11DB7U, 0xFFFFFFFFU, true, 0xFFFFFFFFU, CHECK_STRING,
			    sizeof CHECK_STRING) == 0xCBF43926U);
}

static void test_empty_and_null(void)
{
	CHECK(crc16_ccitt_false(CRC16_INIT, CHECK_STRING, 0U) == CRC16_INIT);
	CHECK(crc16_modbus(CRC16_INIT, CHECK_STRING, 0U) == CRC16_INIT);
	CHECK(crc32_ieee(CRC32_INIT, CHECK_STRING, 0U) == CRC32_INIT);
	CHECK(crc16_ccitt_false(0x1234U, NULL, 4U) == 0x1234U);
	CHECK(crc16_modbus(0x1234U, NULL, 4U) == 0x1234U);
	CHECK(crc32_ieee(0x12345678UL, NULL, 4U) == 0x12345678UL);
}

/* Every single byte value: exercises all 16 entries of the CRC-32 table twice over. */
static void test_every_byte_value_against_reference(void)
{
	for (unsigned value = 0U; value < 256U; value++) {
		const uint8_t byte = (uint8_t)value;
		CHECK(crc16_ccitt_false(CRC16_INIT, &byte, 1U) ==
		      reference_crc(16U, 0x1021U, 0xFFFFU, false, 0U, &byte, 1U));
		CHECK(crc16_modbus(CRC16_INIT, &byte, 1U) ==
		      reference_crc(16U, 0x8005U, 0xFFFFU, true, 0U, &byte, 1U));
		CHECK(crc32_ieee(CRC32_INIT, &byte, 1U) ==
		      reference_crc(32U, 0x04C11DB7U, 0xFFFFFFFFU, true, 0xFFFFFFFFU, &byte, 1U));
	}
}

static void test_random_buffers_against_reference_and_chaining(void)
{
	enum { MAX_LENGTH = 300 };
	for (unsigned round = 0U; round < 2000U; round++) {
		uint8_t data[MAX_LENGTH] = {0};
		const size_t length =
		    (size_t)(((unsigned)random_byte() << 8 | random_byte()) % (MAX_LENGTH + 1U));
		for (size_t i = 0U; i < length; i++) {
			data[i] = random_byte();
		}
		const size_t split = (length == 0U) ? 0U : (size_t)random_byte() % (length + 1U);

		const uint16_t ccitt = crc16_ccitt_false(CRC16_INIT, data, length);
		CHECK(ccitt == reference_crc(16U, 0x1021U, 0xFFFFU, false, 0U, data, length));
		CHECK(crc16_ccitt_false(crc16_ccitt_false(CRC16_INIT, data, split), &data[split],
					length - split) == ccitt);

		const uint16_t modbus = crc16_modbus(CRC16_INIT, data, length);
		CHECK(modbus == reference_crc(16U, 0x8005U, 0xFFFFU, true, 0U, data, length));
		CHECK(crc16_modbus(crc16_modbus(CRC16_INIT, data, split), &data[split], length - split) ==
		      modbus);

		const uint32_t crc32 = crc32_ieee(CRC32_INIT, data, length);
		CHECK(crc32 == reference_crc(32U, 0x04C11DB7U, 0xFFFFFFFFU, true, 0xFFFFFFFFU, data, length));
		CHECK(crc32_ieee(crc32_ieee(CRC32_INIT, data, split), &data[split], length - split) == crc32);
	}
}

/* A CRC must change when any single bit of the message changes. */
static void test_every_single_bit_error_is_detected(void)
{
	uint8_t data[32];
	for (size_t i = 0U; i < sizeof data; i++) {
		data[i] = random_byte();
	}
	const uint16_t ccitt = crc16_ccitt_false(CRC16_INIT, data, sizeof data);
	const uint16_t modbus = crc16_modbus(CRC16_INIT, data, sizeof data);
	const uint32_t crc32 = crc32_ieee(CRC32_INIT, data, sizeof data);

	for (size_t bit = 0U; bit < sizeof data * 8U; bit++) {
		data[bit / 8U] ^= (uint8_t)(1U << (bit % 8U));
		CHECK(crc16_ccitt_false(CRC16_INIT, data, sizeof data) != ccitt);
		CHECK(crc16_modbus(CRC16_INIT, data, sizeof data) != modbus);
		CHECK(crc32_ieee(CRC32_INIT, data, sizeof data) != crc32);
		data[bit / 8U] ^= (uint8_t)(1U << (bit % 8U));
	}
}

/*
 * Receiver-side shortcut: running the CRC over message + its own CRC gives a
 * fixed value (the residue). Byte order on the wire must match the bit order:
 * high byte first for the non-reflected CRC, low byte first for the reflected ones.
 */
static void test_residue_over_message_plus_crc(void)
{
	uint8_t buffer[sizeof CHECK_STRING + 4U];
	memcpy(buffer, CHECK_STRING, sizeof CHECK_STRING);
	const size_t n = sizeof CHECK_STRING;

	const uint16_t ccitt = crc16_ccitt_false(CRC16_INIT, buffer, n);
	buffer[n] = (uint8_t)(ccitt >> 8);
	buffer[n + 1U] = (uint8_t)(ccitt & 0xFFU);
	CHECK(crc16_ccitt_false(CRC16_INIT, buffer, n + 2U) == 0x0000U);

	const uint16_t modbus = crc16_modbus(CRC16_INIT, buffer, n);
	buffer[n] = (uint8_t)(modbus & 0xFFU);
	buffer[n + 1U] = (uint8_t)(modbus >> 8);
	CHECK(crc16_modbus(CRC16_INIT, buffer, n + 2U) == 0x0000U);

	const uint32_t crc32 = crc32_ieee(CRC32_INIT, buffer, n);
	for (unsigned i = 0U; i < 4U; i++) {
		buffer[n + i] = (uint8_t)((crc32 >> (8U * i)) & 0xFFU);
	}
	CHECK(crc32_ieee(CRC32_INIT, buffer, n + 4U) == 0x2144DF1CUL);
}

int main(void)
{
	test_published_check_values();
	test_reference_agrees_with_check_values();
	test_empty_and_null();
	test_every_byte_value_against_reference();
	test_random_buffers_against_reference_and_chaining();
	test_every_single_bit_error_is_detected();
	test_residue_over_message_plus_crc();

	if (failures != 0) {
		fprintf(stderr, "crc: %d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	printf("crc: all tests passed\n");
	return EXIT_SUCCESS;
}
