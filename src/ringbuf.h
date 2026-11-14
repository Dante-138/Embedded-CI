/*
 * ringbuf: fixed-size byte FIFO for one producer and one consumer.
 *
 * Typical use: the UART RX interrupt calls ringbuf_put(), the main loop calls
 * ringbuf_get(). No lock and no interrupt masking are needed for that pair:
 * the producer only writes `head`, the consumer only writes `tail`, and each
 * side reads the index of the other once per call.
 *
 * That holds while reading and writing a size_t is a single instruction on the
 * target (true for 32-bit MCUs such as Cortex-M and RISC-V). On an 8 or 16-bit
 * MCU, or with more than one producer or more than one consumer, protect the
 * calls with a critical section.
 *
 * Design rules:
 * - No dynamic allocation: the caller provides the storage.
 * - One slot stays empty to tell "full" from "empty", so a buffer of `size`
 *   bytes holds at most `size - 1`.
 * - A full buffer rejects new bytes (the oldest data is kept). The caller
 *   counts the drops.
 */
#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Treat the fields as private: use the functions below. */
typedef struct {
    uint8_t *data;
    size_t size;          /* storage size; capacity is size - 1 */
    volatile size_t head; /* next write position, written only by the producer */
    volatile size_t tail; /* next read position, written only by the consumer */
} ringbuf_t;

/* Binds the ring to its storage. Returns false if storage is NULL or size < 2. */
bool ringbuf_init(ringbuf_t *ring, uint8_t *storage, size_t size);

/* Producer side. Returns false (byte dropped) when the ring is full. */
bool ringbuf_put(ringbuf_t *ring, uint8_t byte);

/* Consumer side. Returns false (*byte untouched) when the ring is empty. */
bool ringbuf_get(ringbuf_t *ring, uint8_t *byte);

/* Producer side. Writes up to length bytes; returns how many fit. */
size_t ringbuf_write(ringbuf_t *ring, const uint8_t *data, size_t length);

/* Consumer side. Reads up to length bytes; returns how many were read. */
size_t ringbuf_read(ringbuf_t *ring, uint8_t *data, size_t length);

/* Bytes waiting to be read. */
size_t ringbuf_count(const ringbuf_t *ring);

/* Bytes that can still be written. */
size_t ringbuf_space(const ringbuf_t *ring);

/* Largest number of bytes the ring can hold (size - 1). */
size_t ringbuf_capacity(const ringbuf_t *ring);

#endif /* RINGBUF_H */
