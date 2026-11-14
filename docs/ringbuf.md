# ringbuf

Fixed-size byte FIFO for one producer and one consumer:
[`src/ringbuf.h`](../src/ringbuf.h), [`src/ringbuf.c`](../src/ringbuf.c).

## Problem it solves

Bytes arrive in an interrupt faster than the main loop reads them. Without a
queue between the two, bytes are lost, or shared state is corrupted when the
interrupt fires in the middle of a read.

## Behavior

- One producer (for example the UART RX interrupt) and one consumer (the main
  loop) need no lock and no interrupt masking: the producer only writes `head`,
  the consumer only writes `tail`.
- That holds while a `size_t` is read and written in one instruction (32-bit
  MCUs). On 8 or 16-bit MCUs, or with several producers or consumers, wrap the
  calls in a critical section.
- The caller provides the storage. A buffer of `size` bytes holds `size - 1`.
- A full ring rejects the new byte and keeps the old data; `ringbuf_put()`
  returns `false` so the caller can count drops.

## Usage

```c
#include "ringbuf.h"

static uint8_t rx_storage[128];
static ringbuf_t rx_ring;
static uint32_t rx_dropped;

void uart_init(void)
{
    (void)ringbuf_init(&rx_ring, rx_storage, sizeof rx_storage);
}

void uart_rx_isr(uint8_t byte) /* producer */
{
    if (!ringbuf_put(&rx_ring, byte)) {
        rx_dropped++;
    }
}

void main_loop_step(void) /* consumer */
{
    uint8_t byte;
    while (ringbuf_get(&rx_ring, &byte)) {
        handle_byte(byte);
    }
}
```

## Tests ([tests/test_ringbuf.c](../tests/test_ringbuf.c))

Run with ASan + UBSan; the storage is a heap block of the exact size, so one
byte outside it aborts the run.

- `ringbuf_init()` rejects `NULL` and sizes below 2.
- `NULL`, a zeroed (not yet initialized) ring and `NULL` data pointers are
  safe: nothing is read, written or consumed.
- FIFO order, a full ring rejects the new byte, an empty ring leaves the output
  untouched.
- 1,000 rounds crossing the end of the storage.
- Block write and read: partial writes, no byte written past what was asked.
- Reference model: 20,000 random puts and gets for every size from 2 to 33,
  checking each result, the byte order and the count after every operation.

Not covered by the host tests: real concurrency between an interrupt and the
main loop. The lock-free claim rests on the single-writer rule per index
described above, not on a test.

## Copy into a project

Copy `src/ringbuf.c` and `src/ringbuf.h`. No other file is needed.
