/*
 * Host tests for src/frame.c. Built with ASan + UBSan by the Makefile, so any
 * out-of-bounds access or undefined behavior aborts the run, not only a
 * failed CHECK.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame.h"

#define FRAME_BUFFER_SIZE (FRAME_MAX_PAYLOAD + FRAME_OVERHEAD)

static int failures;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                             \
        }                                                                           \
    } while (0)

/* Deterministic pseudo-random generator (xorshift32): same bytes on every run. */
static uint32_t rng_state = 0x12345678U;

static uint8_t random_byte(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (uint8_t)(rng_state & 0xFFU);
}

/*
 * Comparing a uint8_t directly with FRAME_MAX_PAYLOAD is "always true" when the
 * limit is 255, and -Wtype-limits makes that a build error. Same fix as in
 * frame.c: compare through a wider parameter.
 */
static bool within_limit(unsigned length)
{
    return length <= FRAME_MAX_PAYLOAD;
}

static uint8_t random_non_sof(void)
{
    const uint8_t byte = random_byte();
    return (byte == FRAME_SOF) ? (uint8_t)0x00U : byte;
}

static void fill_pattern(uint8_t *data, size_t length, uint8_t seed)
{
    for (size_t i = 0U; i < length; i++) {
        data[i] = (uint8_t)(seed + (uint8_t)i);
    }
}

/* Feeds every byte; returns how many frames came out READY. */
static unsigned feed_all(frame_parser_t *parser, const uint8_t *bytes, size_t count)
{
    unsigned ready = 0U;
    for (size_t i = 0U; i < count; i++) {
        if (frame_parser_feed(parser, bytes[i]) == FRAME_READY) {
            ready++;
        }
    }
    return ready;
}

static void test_crc_check_value(void)
{
    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(frame_crc16(FRAME_CRC16_INIT, check, sizeof check) == 0x29B1U);
    CHECK(frame_crc16(FRAME_CRC16_INIT, check, 0U) == FRAME_CRC16_INIT);
    CHECK(frame_crc16(0x1234U, NULL, 5U) == 0x1234U);

    /* Feeding the data in two parts gives the same CRC as in one go. */
    const uint16_t first = frame_crc16(FRAME_CRC16_INIT, check, 4U);
    CHECK(frame_crc16(first, &check[4], sizeof check - 4U) == 0x29B1U);
}

static void test_encode_layout(void)
{
    const uint8_t payload[] = {0x01U, 0x02U, 0x03U};
    uint8_t out[FRAME_BUFFER_SIZE];
    CHECK(frame_encode(payload, 3U, out, sizeof out) == 7U);
    CHECK(out[0] == FRAME_SOF);
    CHECK(out[1] == 3U);
    CHECK(memcmp(&out[2], payload, 3U) == 0);
    const uint16_t crc = frame_crc16(FRAME_CRC16_INIT, &out[1], 4U);
    CHECK(out[5] == (uint8_t)(crc >> 8));
    CHECK(out[6] == (uint8_t)(crc & 0xFFU));
}

static void test_encode_rejects_bad_arguments(void)
{
    const uint8_t payload[FRAME_MAX_PAYLOAD] = {0};
    uint8_t out[FRAME_BUFFER_SIZE];
    CHECK(frame_encode(payload, 1U, out, 4U) == 0U); /* needs 5 bytes */
    CHECK(frame_encode(payload, 1U, NULL, sizeof out) == 0U);
    CHECK(frame_encode(NULL, 1U, out, sizeof out) == 0U);
    CHECK(frame_encode(NULL, 0U, out, sizeof out) == FRAME_OVERHEAD); /* empty frame is valid */
#if FRAME_MAX_PAYLOAD < 255U
    CHECK(frame_encode(payload, (uint8_t)(FRAME_MAX_PAYLOAD + 1U), out, sizeof out + 1U) == 0U);
#endif
}

static void test_round_trip_every_length(void)
{
    for (unsigned length = 0U; length <= FRAME_MAX_PAYLOAD; length++) {
        uint8_t payload[FRAME_MAX_PAYLOAD];
        uint8_t wire[FRAME_BUFFER_SIZE];
        fill_pattern(payload, length, (uint8_t)length);
        const size_t size = frame_encode(payload, (uint8_t)length, wire, sizeof wire);
        CHECK(size == length + FRAME_OVERHEAD);

        frame_parser_t parser;
        frame_parser_init(&parser);
        /* Every byte but the last is PENDING; the last one completes the frame. */
        for (size_t i = 0U; i + 1U < size; i++) {
            CHECK(frame_parser_feed(&parser, wire[i]) == FRAME_PENDING);
        }
        CHECK(frame_parser_feed(&parser, wire[size - 1U]) == FRAME_READY);

        uint8_t got_length = 0xEEU;
        const uint8_t *got = frame_parser_payload(&parser, &got_length);
        CHECK(got != NULL);
        CHECK(got_length == length);
        CHECK(got != NULL && memcmp(got, payload, length) == 0);
    }
}

static void test_payload_only_right_after_ready(void)
{
    frame_parser_t parser;
    frame_parser_init(&parser);
    uint8_t length = 0xEEU;
    CHECK(frame_parser_payload(&parser, &length) == NULL);
    CHECK(length == 0U);

    const uint8_t payload[] = {0x42U};
    uint8_t wire[FRAME_BUFFER_SIZE];
    const size_t size = frame_encode(payload, 1U, wire, sizeof wire);
    CHECK(feed_all(&parser, wire, size) == 1U);
    CHECK(frame_parser_payload(&parser, NULL) != NULL);

    (void)frame_parser_feed(&parser, 0x00U); /* any next byte invalidates it */
    CHECK(frame_parser_payload(&parser, &length) == NULL);
    CHECK(length == 0U);
    CHECK(frame_parser_payload(NULL, &length) == NULL);
}

static void test_skips_leading_garbage(void)
{
    const uint8_t garbage[] = {0x00U, 0xFFU, 0x13U, 0x37U, 0x5AU};
    const uint8_t payload[] = {0x10U, 0x20U};
    uint8_t wire[FRAME_BUFFER_SIZE];
    const size_t size = frame_encode(payload, 2U, wire, sizeof wire);

    frame_parser_t parser;
    frame_parser_init(&parser);
    CHECK(feed_all(&parser, garbage, sizeof garbage) == 0U);
    CHECK(feed_all(&parser, wire, size) == 1U);
}

static void test_crc_error_then_recovers(void)
{
    const uint8_t payload[] = {0xDEU, 0xADU, 0xBEU, 0xEFU};
    uint8_t wire[FRAME_BUFFER_SIZE];
    const size_t size = frame_encode(payload, 4U, wire, sizeof wire);

    frame_parser_t parser;
    frame_parser_init(&parser);

    /* Flip one bit in each position past SOF/LEN: every single-bit error is caught. */
    for (size_t position = 2U; position < size; position++) {
        uint8_t bad[FRAME_BUFFER_SIZE];
        memcpy(bad, wire, size);
        bad[position] ^= 0x01U;
        frame_status_t last = FRAME_PENDING;
        for (size_t i = 0U; i < size; i++) {
            last = frame_parser_feed(&parser, bad[i]);
        }
        CHECK(last == FRAME_ERROR_CRC);
        CHECK(frame_parser_payload(&parser, NULL) == NULL);
    }
    /* The next good frame goes through with no flush or reset by the caller. */
    CHECK(feed_all(&parser, wire, size) == 1U);
}

static void test_length_error_then_recovers(void)
{
#if FRAME_MAX_PAYLOAD < 255U
    const uint8_t too_long[] = {FRAME_SOF, (uint8_t)(FRAME_MAX_PAYLOAD + 1U)};
    const uint8_t payload[] = {0x01U};
    uint8_t wire[FRAME_BUFFER_SIZE];
    const size_t size = frame_encode(payload, 1U, wire, sizeof wire);

    frame_parser_t parser;
    frame_parser_init(&parser);
    CHECK(frame_parser_feed(&parser, too_long[0]) == FRAME_PENDING);
    if (too_long[1] != FRAME_SOF) {
        CHECK(frame_parser_feed(&parser, too_long[1]) == FRAME_ERROR_LENGTH);
    }
    CHECK(feed_all(&parser, wire, size) == 1U);
#endif
}

static void test_repeated_sof_is_a_new_start(void)
{
#if FRAME_MAX_PAYLOAD < FRAME_SOF
    /* A stray SOF just before a real frame must not cost us that frame. */
    const uint8_t payload[] = {0x77U, 0x88U};
    uint8_t wire[FRAME_BUFFER_SIZE];
    const size_t size = frame_encode(payload, 2U, wire, sizeof wire);

    frame_parser_t parser;
    frame_parser_init(&parser);
    CHECK(frame_parser_feed(&parser, FRAME_SOF) == FRAME_PENDING);
    CHECK(frame_parser_feed(&parser, FRAME_SOF) == FRAME_PENDING);
    CHECK(feed_all(&parser, wire, size) == 1U);
#endif
}

static void test_back_to_back_frames(void)
{
    uint8_t stream[3U * FRAME_BUFFER_SIZE];
    size_t used = 0U;
    for (uint8_t n = 0U; n < 3U; n++) {
        uint8_t payload[3];
        fill_pattern(payload, sizeof payload, (uint8_t)(n * 16U));
        used += frame_encode(payload, sizeof payload, &stream[used], sizeof stream - used);
    }
    frame_parser_t parser;
    frame_parser_init(&parser);
    CHECK(feed_all(&parser, stream, used) == 3U);
}

static void test_init_drops_partial_frame(void)
{
    const uint8_t payload[] = {0x01U, 0x02U, 0x03U, 0x04U};
    uint8_t wire[FRAME_BUFFER_SIZE];
    const size_t size = frame_encode(payload, 4U, wire, sizeof wire);

    frame_parser_t parser;
    frame_parser_init(&parser);
    CHECK(feed_all(&parser, wire, size / 2U) == 0U); /* link goes quiet mid-frame */
    frame_parser_init(&parser);                     /* caller's idle timeout fires */
    CHECK(feed_all(&parser, wire, size) == 1U);
}

static void test_corrupted_state_restarts(void)
{
    frame_parser_t parser;
    frame_parser_init(&parser);
    memset(&parser.state, 0x7F, sizeof parser.state); /* not a valid state */
    CHECK(frame_parser_feed(&parser, FRAME_SOF) == FRAME_PENDING);

    const uint8_t payload[] = {0x55U};
    uint8_t wire[FRAME_BUFFER_SIZE];
    const size_t size = frame_encode(payload, 1U, wire, sizeof wire);
    CHECK(feed_all(&parser, wire, size) == 1U);
    CHECK(frame_parser_feed(NULL, 0x00U) == FRAME_PENDING);
}

/* Random bytes only: the parser must never crash, overflow or report a bad frame. */
static void test_random_noise(void)
{
    frame_parser_t parser;
    frame_parser_init(&parser);
    for (unsigned i = 0U; i < 200000U; i++) {
        if (frame_parser_feed(&parser, random_byte()) == FRAME_READY) {
            uint8_t length = 0U;
            const uint8_t *got = frame_parser_payload(&parser, &length);
            CHECK(got != NULL);
            CHECK(within_limit(length));
        }
    }
}

/* Random frames with random noise (no SOF) in between: every frame must come out intact. */
static void test_frames_survive_noise(void)
{
    frame_parser_t parser;
    frame_parser_init(&parser);
    for (unsigned n = 0U; n < 2000U; n++) {
        const uint8_t noise = (uint8_t)(random_byte() % 17U);
        for (uint8_t i = 0U; i < noise; i++) {
            CHECK(frame_parser_feed(&parser, random_non_sof()) == FRAME_PENDING);
        }

        uint8_t payload[FRAME_MAX_PAYLOAD] = {0};
        const uint8_t length = (uint8_t)(random_byte() % (FRAME_MAX_PAYLOAD + 1U));
        for (uint8_t i = 0U; i < length; i++) {
            payload[i] = random_byte();
        }
        uint8_t wire[FRAME_BUFFER_SIZE];
        const size_t size = frame_encode(payload, length, wire, sizeof wire);
        CHECK(feed_all(&parser, wire, size) == 1U);

        uint8_t got_length = 0U;
        const uint8_t *got = frame_parser_payload(&parser, &got_length);
        CHECK(got != NULL && got_length == length && memcmp(got, payload, length) == 0);
    }
}

/* Encodes a random frame into wire; returns its size. */
static size_t random_frame(uint8_t *payload, uint8_t *length, uint8_t *wire, size_t wire_size)
{
    *length = (uint8_t)(random_byte() % (FRAME_MAX_PAYLOAD + 1U));
    for (uint8_t i = 0U; i < *length; i++) {
        payload[i] = random_byte();
    }
    return frame_encode(payload, *length, wire, wire_size);
}

/*
 * Resynchronization bound: whatever state noise (SOF included) leaves the
 * parser in, at most FRAME_MAX_PAYLOAD + 3 bytes without SOF bring it back to
 * hunting for SOF. Worst case: it has just taken a false SOF and still reads
 * LEN, a full payload and two CRC bytes. After that the next frame always parses.
 */
static void test_resync_bound_after_noise_with_sof(void)
{
    frame_parser_t parser;
    frame_parser_init(&parser);
    for (unsigned n = 0U; n < 2000U; n++) {
        const unsigned noise = random_byte() % 64U;
        for (unsigned i = 0U; i < noise; i++) {
            (void)frame_parser_feed(&parser, random_byte());
        }
        if ((n % 2U) != 0U) {
            (void)frame_parser_feed(&parser, FRAME_SOF); /* force a false start */
        }
        for (unsigned i = 0U; i < FRAME_MAX_PAYLOAD + 3U; i++) {
            (void)frame_parser_feed(&parser, random_non_sof());
        }

        uint8_t payload[FRAME_MAX_PAYLOAD] = {0};
        uint8_t length = 0U;
        uint8_t wire[FRAME_BUFFER_SIZE];
        const size_t size = random_frame(payload, &length, wire, sizeof wire);
        CHECK(feed_all(&parser, wire, size) == 1U);

        uint8_t got_length = 0U;
        const uint8_t *got = frame_parser_payload(&parser, &got_length);
        CHECK(got != NULL && got_length == length && memcmp(got, payload, length) == 0);
    }
}

/*
 * Noise that may contain SOF, with no gap before the next frame. The parser
 * does not backtrack: a false SOF in the noise can make it read the start of
 * the next real frame as payload, and that frame is lost. This measures the
 * loss and checks that, with this seed, no frame is delivered with the wrong
 * content.
 */
static void test_loss_with_sof_in_noise(void)
{
    enum { FRAMES = 20000 };
    unsigned lost = 0U;
    unsigned streak = 0U;
    unsigned worst_streak = 0U;
    unsigned wrong = 0U;

    frame_parser_t parser;
    frame_parser_init(&parser);
    for (unsigned n = 0U; n < (unsigned)FRAMES; n++) {
        const unsigned noise = random_byte() % 17U;
        for (unsigned i = 0U; i < noise; i++) {
            if (frame_parser_feed(&parser, random_byte()) == FRAME_READY) {
                wrong++; /* a frame made of noise only */
            }
        }

        uint8_t payload[FRAME_MAX_PAYLOAD] = {0};
        uint8_t length = 0U;
        uint8_t wire[FRAME_BUFFER_SIZE];
        const size_t size = random_frame(payload, &length, wire, sizeof wire);
        bool received = false;
        for (size_t i = 0U; i < size; i++) {
            if (frame_parser_feed(&parser, wire[i]) == FRAME_READY) {
                uint8_t got_length = 0U;
                const uint8_t *got = frame_parser_payload(&parser, &got_length);
                if (got != NULL && got_length == length && memcmp(got, payload, length) == 0) {
                    received = true;
                } else {
                    wrong++;
                }
            }
        }

        if (received) {
            streak = 0U;
        } else {
            lost++;
            streak++;
            if (streak > worst_streak) {
                worst_streak = streak;
            }
        }
    }

    printf("frame: SOF in noise, limit %u: lost %u of %u frames (worst run %u in a row), %u wrong\n",
           (unsigned)FRAME_MAX_PAYLOAD, lost, (unsigned)FRAMES, worst_streak, wrong);
    /*
     * Zero holds for this byte sequence, not in general: CRC-16 accepts a
     * random false frame with a chance of about 1 in 65,536. The sequence comes
     * from one shared generator, so adding or changing another test shifts it.
     * If this check then fails with a false frame whose CRC matches, changing
     * the seed (rng_state) is legitimate; a frame with a CRC that does not match
     * is a real bug.
     */
    CHECK(wrong == 0U);
    CHECK(lost * 10U < (unsigned)FRAMES); /* regression guard: under 10% lost */
}

int main(void)
{
    test_crc_check_value();
    test_encode_layout();
    test_encode_rejects_bad_arguments();
    test_round_trip_every_length();
    test_payload_only_right_after_ready();
    test_skips_leading_garbage();
    test_crc_error_then_recovers();
    test_length_error_then_recovers();
    test_repeated_sof_is_a_new_start();
    test_back_to_back_frames();
    test_init_drops_partial_frame();
    test_corrupted_state_restarts();
    test_random_noise();
    test_frames_survive_noise();
    test_resync_bound_after_noise_with_sof();
    test_loss_with_sof_in_noise();

    if (failures != 0) {
        fprintf(stderr, "frame: %d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("frame: all tests passed\n");
    return EXIT_SUCCESS;
}
