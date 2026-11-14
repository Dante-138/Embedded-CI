/*
 * frame: byte-by-byte parser and encoder for a length-prefixed serial frame.
 *
 * Wire format (CRC is big-endian):
 *
 *   +------+-----+------------------+--------+--------+
 *   | SOF  | LEN | PAYLOAD[LEN]     | CRC_HI | CRC_LO |
 *   +------+-----+------------------+--------+--------+
 *     0xA5   1 B   0..FRAME_MAX_PAYLOAD bytes
 *
 * CRC is CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no
 * final XOR) over LEN and PAYLOAD. Its published check value is 0x29B1 for
 * the ASCII string "123456789".
 *
 * Design rules (firmware style):
 * - No dynamic allocation: the parser owns a fixed payload buffer, so it can
 *   be a static or stack object.
 * - One byte in, one status out: call frame_parser_feed() from the UART RX
 *   path (main loop or a task reading a ring buffer), not from the ISR, since
 *   the CRC check runs on the last byte.
 * - Self-resynchronizing: after a bad length or a CRC mismatch the parser goes
 *   back to hunting for SOF. The caller never has to flush anything. Whatever
 *   state noise leaves it in, at most FRAME_MAX_PAYLOAD + 3 bytes without SOF
 *   bring it back to hunting.
 * - No backtracking: bytes already consumed are not rescanned. A false SOF in
 *   line noise can make the parser read the start of the next real frame as
 *   payload, and that frame is lost (reported as FRAME_ERROR_CRC or
 *   FRAME_ERROR_LENGTH). The cost is lower with a smaller FRAME_MAX_PAYLOAD,
 *   since a false start swallows fewer bytes. The upside: O(1) memory and no
 *   second buffer.
 * - A repeated SOF (SOF where LEN was expected) is taken as a new start, so a
 *   stray SOF just before a real frame does not cost that frame. This only
 *   applies while FRAME_MAX_PAYLOAD < 0xA5: from 0xA5 up, SOF is also a valid
 *   LEN and is read as one.
 * - No timing inside: if the link has an inter-byte timeout (UART idle line),
 *   call frame_parser_init() when it fires to drop a half-received frame.
 */
#ifndef FRAME_H
#define FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Largest payload accepted. Override at build time with -DFRAME_MAX_PAYLOAD=n. */
#ifndef FRAME_MAX_PAYLOAD
#define FRAME_MAX_PAYLOAD 64U
#endif

#define FRAME_SOF        0xA5U
#define FRAME_OVERHEAD   4U /* SOF + LEN + CRC_HI + CRC_LO */
#define FRAME_CRC16_INIT 0xFFFFU

_Static_assert(FRAME_MAX_PAYLOAD >= 1U && FRAME_MAX_PAYLOAD <= 255U, "LEN is a single byte");

typedef enum {
    FRAME_PENDING = 0,  /* byte consumed, no complete frame yet */
    FRAME_READY,        /* a valid frame is available from frame_parser_payload() */
    FRAME_ERROR_LENGTH, /* LEN above FRAME_MAX_PAYLOAD: frame dropped, hunting for SOF */
    FRAME_ERROR_CRC     /* CRC mismatch: frame dropped, hunting for SOF */
} frame_status_t;

/* Internal states. Public only so the parser can be allocated by the caller. */
typedef enum {
    FRAME_STATE_SOF = 0,
    FRAME_STATE_LENGTH,
    FRAME_STATE_PAYLOAD,
    FRAME_STATE_CRC_HIGH,
    FRAME_STATE_CRC_LOW
} frame_state_t;

/* Treat the fields as private: use the functions below. */
typedef struct {
    frame_state_t state;
    uint8_t length;   /* LEN of the frame being received */
    uint8_t received; /* payload bytes stored so far */
    uint16_t crc_wire; /* high byte of the CRC read from the wire */
    bool ready;       /* true only right after feed() returned FRAME_READY */
    uint8_t payload[FRAME_MAX_PAYLOAD];
} frame_parser_t;

/* Puts the parser in its initial state. Also the way to drop a partial frame. */
void frame_parser_init(frame_parser_t *parser);

/* Feeds one received byte. Returns what that byte completed, if anything. */
frame_status_t frame_parser_feed(frame_parser_t *parser, uint8_t byte);

/*
 * Payload of the frame just completed, or NULL if the last feed() did not
 * return FRAME_READY. Valid until the next call to feed() or init().
 * *length receives the payload size (0 when NULL is returned); it may be NULL.
 */
const uint8_t *frame_parser_payload(const frame_parser_t *parser, uint8_t *length);

/*
 * Builds a complete frame in out. Returns the number of bytes written
 * (length + FRAME_OVERHEAD), or 0 if an argument is invalid or out is too
 * small. payload may be NULL only when length is 0. payload and out must not
 * overlap.
 */
size_t frame_encode(const uint8_t *payload, uint8_t length, uint8_t *out, size_t out_size);

/*
 * CRC-16/CCITT-FALSE, bitwise. Start with FRAME_CRC16_INIT; pass the result
 * back in to continue over more data.
 */
uint16_t frame_crc16(uint16_t crc, const uint8_t *data, size_t length);

#endif /* FRAME_H */
