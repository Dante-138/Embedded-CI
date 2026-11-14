#include "crc.h"

#define CRC16_CCITT_POLY 0x1021U
/* 0x8005 with the bits reversed: a reflected CRC shifts right, so the poly is mirrored. */
#define CRC16_MODBUS_POLY_REFLECTED 0xA001U

uint16_t crc16_ccitt_false(uint16_t crc, const uint8_t *data, size_t length)
{
    if (data == NULL) {
        return crc;
    }
    for (size_t i = 0U; i < length; i++) {
        /*
         * The casts are needed because C promotes uint16_t to int before << and
         * ^; without them -Wconversion rejects the assignment back to uint16_t.
         */
        crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)data[i] << 8));
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ CRC16_CCITT_POLY);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint16_t crc16_modbus(uint16_t crc, const uint8_t *data, size_t length)
{
    if (data == NULL) {
        return crc;
    }
    for (size_t i = 0U; i < length; i++) {
        crc = (uint16_t)(crc ^ data[i]);
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (uint16_t)((crc >> 1) ^ CRC16_MODBUS_POLY_REFLECTED);
            } else {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}

/*
 * CRC-32 of each 4-bit value, reflected poly 0xEDB88320. The host test rebuilds
 * the result bit by bit and compares, so a typo in this table cannot pass.
 */
static const uint32_t crc32_nibble_table[16] = {
    0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
    0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
    0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
    0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL,
};

uint32_t crc32_ieee(uint32_t crc, const uint8_t *data, size_t length)
{
    if (data == NULL) {
        return crc;
    }
    /*
     * Inverting on the way in and on the way out gives the 0xFFFFFFFF init and
     * final XOR, and lets the caller chain calls with the plain result.
     */
    crc = ~crc;
    for (size_t i = 0U; i < length; i++) {
        crc ^= data[i];
        crc = (crc >> 4) ^ crc32_nibble_table[crc & 0x0FU];
        crc = (crc >> 4) ^ crc32_nibble_table[crc & 0x0FU];
    }
    return ~crc;
}
