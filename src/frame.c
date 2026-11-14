#include "frame.h"

#include <string.h>

#define CRC16_POLY 0x1021U

/*
 * Kept as a function taking uint32_t on purpose: comparing a uint8_t directly
 * against FRAME_MAX_PAYLOAD would be "always true" when the limit is 255, and
 * -Wtype-limits (part of -Wextra) turns that into a build error.
 */
static bool length_is_valid(uint32_t length)
{
    return length <= FRAME_MAX_PAYLOAD;
}

uint16_t frame_crc16(uint16_t crc, const uint8_t *data, size_t length)
{
    if (data == NULL) {
        return crc;
    }
    for (size_t i = 0U; i < length; i++) {
        crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)data[i] << 8));
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ CRC16_POLY);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

void frame_parser_init(frame_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }
    parser->state = FRAME_STATE_SOF;
    parser->length = 0U;
    parser->received = 0U;
    parser->crc_wire = 0U;
    parser->ready = false;
}

frame_status_t frame_parser_feed(frame_parser_t *parser, uint8_t byte)
{
    if (parser == NULL) {
        return FRAME_PENDING;
    }
    parser->ready = false;

    switch (parser->state) {
    case FRAME_STATE_SOF:
        if (byte == FRAME_SOF) {
            parser->state = FRAME_STATE_LENGTH;
        }
        return FRAME_PENDING;

    case FRAME_STATE_LENGTH:
        if (!length_is_valid(byte)) {
            /*
             * A second SOF right after the first (the real LEN was lost or
             * the first SOF was noise) is treated as a new start instead of
             * an error, so the frame that follows is not lost.
             */
            if (byte == FRAME_SOF) {
                return FRAME_PENDING;
            }
            parser->state = FRAME_STATE_SOF;
            return FRAME_ERROR_LENGTH;
        }
        parser->length = byte;
        parser->received = 0U;
        parser->state = (byte == 0U) ? FRAME_STATE_CRC_HIGH : FRAME_STATE_PAYLOAD;
        return FRAME_PENDING;

    case FRAME_STATE_PAYLOAD:
        /* received < length <= FRAME_MAX_PAYLOAD here, so the index is in bounds. */
        parser->payload[parser->received] = byte;
        parser->received++;
        if (parser->received == parser->length) {
            parser->state = FRAME_STATE_CRC_HIGH;
        }
        return FRAME_PENDING;

    case FRAME_STATE_CRC_HIGH:
        parser->crc_wire = (uint16_t)((uint16_t)byte << 8);
        parser->state = FRAME_STATE_CRC_LOW;
        return FRAME_PENDING;

    case FRAME_STATE_CRC_LOW: {
        const uint16_t wire = (uint16_t)(parser->crc_wire | byte);
        uint16_t crc = frame_crc16(FRAME_CRC16_INIT, &parser->length, 1U);
        crc = frame_crc16(crc, parser->payload, parser->length);
        parser->state = FRAME_STATE_SOF;
        if (crc != wire) {
            return FRAME_ERROR_CRC;
        }
        parser->ready = true;
        return FRAME_READY;
    }

    default:
        /* Corrupted state (e.g. stray write into the struct): start over. */
        frame_parser_init(parser);
        return FRAME_PENDING;
    }
}

const uint8_t *frame_parser_payload(const frame_parser_t *parser, uint8_t *length)
{
    const bool ready = (parser != NULL) && parser->ready;
    if (length != NULL) {
        *length = ready ? parser->length : (uint8_t)0U;
    }
    return ready ? parser->payload : NULL;
}

size_t frame_encode(const uint8_t *payload, uint8_t length, uint8_t *out, size_t out_size)
{
    const size_t total = (size_t)length + FRAME_OVERHEAD;
    if (out == NULL || !length_is_valid(length) || out_size < total ||
        (payload == NULL && length > 0U)) {
        return 0U;
    }
    out[0] = FRAME_SOF;
    out[1] = length;
    if (length > 0U) {
        memcpy(&out[2], payload, length);
    }
    const uint16_t crc = frame_crc16(FRAME_CRC16_INIT, &out[1], (size_t)length + 1U);
    out[total - 2U] = (uint8_t)(crc >> 8);
    out[total - 1U] = (uint8_t)(crc & 0xFFU);
    return total;
}
