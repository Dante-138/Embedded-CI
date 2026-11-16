/*
 * crc: the three CRCs most firmware ends up needing, each one checked against
 * its published check value (the CRC of the ASCII string "123456789").
 *
 *   Function           Catalogue name       Poly        Init        Reflected  Final XOR   Check
 *   crc16_ccitt_false  CRC-16/IBM-3740      0x1021      0xFFFF      no         0x0000      0x29B1
 *   crc16_modbus       CRC-16/MODBUS        0x8005      0xFFFF      yes        0x0000      0x4B37
 *   crc32_ieee         CRC-32/ISO-HDLC      0x04C11DB7  0xFFFFFFFF  yes        0xFFFFFFFF  0xCBF43926
 *
 * A CRC that "does not match the other side" is almost always one of these
 * parameters: bit order (reflected or not), initial value or final XOR. The
 * table above is the contract; compare it with the other side first.
 *
 * All functions can run over the data in pieces: pass the previous result back
 * in as `crc`.
 *
 * Size and speed: the 16-bit CRCs are bitwise (no table, 8 shifts per byte).
 * crc32_ieee uses a 16-entry table (64 bytes of ROM, 2 lookups per byte), a
 * middle ground between bitwise and the usual 1 KiB table.
 *
 * On the wire: crc16_modbus is sent low byte first by Modbus RTU; the byte
 * order of the others is defined by each protocol.
 */
#ifndef CRC_H
#define CRC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start value for crc16_ccitt_false() and crc16_modbus(). */
#define CRC16_INIT 0xFFFFU

/* Start value for crc32_ieee(). The 0xFFFFFFFF init and final XOR happen inside. */
#define CRC32_INIT 0x00000000UL

/* CRC-16/CCITT-FALSE. First call: crc = CRC16_INIT. */
uint16_t crc16_ccitt_false(uint16_t crc, const uint8_t *data, size_t length);

/* CRC-16/MODBUS. First call: crc = CRC16_INIT. */
uint16_t crc16_modbus(uint16_t crc, const uint8_t *data, size_t length);

/* CRC-32 (Ethernet, zip, PNG). First call: crc = CRC32_INIT. */
uint32_t crc32_ieee(uint32_t crc, const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* CRC_H */
