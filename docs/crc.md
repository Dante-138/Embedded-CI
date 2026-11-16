# crc

Three standalone CRCs: [`src/crc.h`](../src/crc.h), [`src/crc.c`](../src/crc.c).

| Function | Catalogue name | Poly | Init | Reflected | Final XOR | Check (`"123456789"`) |
| --- | --- | --- | --- | --- | --- | --- |
| `crc16_ccitt_false` | CRC-16/IBM-3740 | `0x1021` | `0xFFFF` | no | `0x0000` | `0x29B1` |
| `crc16_modbus` | CRC-16/MODBUS | `0x8005` | `0xFFFF` | yes | `0x0000` | `0x4B37` |
| `crc32_ieee` | CRC-32/ISO-HDLC | `0x04C11DB7` | `0xFFFFFFFF` | yes | `0xFFFFFFFF` | `0xCBF43926` |

## Problem it solves

A CRC that does not match the other side of the link. The cause is almost
always a parameter (bit order, initial value, final XOR), not the code. The
table above is the contract of each function: compare it with the other side
before debugging anything else.

## Behavior

- C11, no allocation, no global state.
- Every function runs over the data in pieces: pass the previous result back
  in. `crc32_ieee` does its `0xFFFFFFFF` init and final XOR inside, so chaining
  uses the plain result.
- The 16-bit CRCs are bitwise (no table). `crc32_ieee` uses a 16-entry table:
  64 bytes of ROM and 2 lookups per byte, between bitwise and the usual 1 KiB
  table.
- `NULL` data returns the CRC unchanged.

## Usage

```c
#include "crc.h"

/* One shot */
uint16_t crc = crc16_modbus(CRC16_INIT, frame, frame_length);

/* In pieces (header and payload in different buffers) */
uint32_t sum = crc32_ieee(CRC32_INIT, header, header_length);
sum = crc32_ieee(sum, payload, payload_length);

/* Modbus RTU sends the CRC low byte first */
out[n] = (uint8_t)(crc & 0xFFU);
out[n + 1] = (uint8_t)(crc >> 8);
```

## Tests ([tests/test_crc.c](../tests/test_crc.c))

Run with ASan + UBSan.

- The published check value of each CRC.
- An independent reference written in the test from the catalogue parameters,
  bit by bit, sharing no code and no table with the module. The reference is
  first checked against the same published values.
- Every single byte value (0 to 255) against the reference, which exercises
  every entry of the CRC-32 table.
- 2,000 random buffers of 0 to 300 bytes against the reference, each also
  computed in two pieces at a random split.
- Every single-bit error in a 32-byte message changes all three CRCs.
- Residue: the CRC over message plus its own CRC is the fixed value expected
  for each algorithm (`0x0000`, `0x0000`, `0x2144DF1C`), with the byte order
  each one needs on the wire.
- Empty input and `NULL` return the CRC unchanged.

## Copy into a project

Copy `src/crc.c` and `src/crc.h`. To save ROM, delete the functions you do not
use. The `frame` module keeps its own CRC-16 so that it stays a single file
pair; it does not depend on this one.
