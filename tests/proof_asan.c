/*
 * Proof that AddressSanitizer is on: writes one byte past the end of a heap
 * buffer through memset. ASan intercepts memset and must report
 * "heap-buffer-overflow"; UBSan does not see it, so this checks ASan alone.
 * "make proof" fails if this program exits normally.
 *
 * Never part of the normal test suite.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame.h"

int main(int argc, char **argv)
{
    (void)argv;
    uint8_t *payload = malloc(FRAME_MAX_PAYLOAD);
    if (payload == NULL) {
        return EXIT_FAILURE;
    }
    /* argc is 1, so this is one byte too many; the compiler cannot see that. */
    memset(payload, 0, (size_t)FRAME_MAX_PAYLOAD + (size_t)argc);
    /*
     * Use the buffer afterwards: a malloc/memset/free whose data is never read
     * is dead code, and GCC removes all three at -O1, bug included.
     */
    printf("%04x\n", (unsigned)frame_crc16(FRAME_CRC16_INIT, payload, FRAME_MAX_PAYLOAD));
    free(payload);
    return EXIT_SUCCESS;
}
