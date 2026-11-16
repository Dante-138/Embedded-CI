#include "ringbuf.h"

#include <errno.h>

static bool is_valid(const ringbuf_t *ring)
{
	return (ring != NULL) && (ring->data != NULL) && (ring->size >= 2U);
}

/* Index after `index`, wrapping at the end. A compare is cheaper than % on an MCU. */
static size_t next_index(const ringbuf_t *ring, size_t index)
{
	index++;
	return (index == ring->size) ? 0U : index;
}

int ringbuf_init(ringbuf_t *ring, uint8_t *storage, size_t size)
{
	if (ring == NULL || storage == NULL || size < 2U) {
		return -EINVAL;
	}
	ring->data = storage;
	ring->size = size;
	ring->head = 0U;
	ring->tail = 0U;
	return 0;
}

bool ringbuf_put(ringbuf_t *ring, uint8_t byte)
{
	if (!is_valid(ring)) {
		return false;
	}
	/* Each volatile index is read once, so the decision uses one snapshot. */
	const size_t head = ring->head;
	const size_t next = next_index(ring, head);
	if (next == ring->tail) {
		return false; /* full */
	}
	ring->data[head] = byte;
	ring->head = next; /* publish only after the byte is in place */
	return true;
}

bool ringbuf_get(ringbuf_t *ring, uint8_t *byte)
{
	if (!is_valid(ring) || byte == NULL) {
		return false;
	}
	const size_t tail = ring->tail;
	if (tail == ring->head) {
		return false; /* empty */
	}
	*byte = ring->data[tail];
	ring->tail = next_index(ring, tail); /* free the slot only after the copy */
	return true;
}

size_t ringbuf_write(ringbuf_t *ring, const uint8_t *data, size_t length)
{
	if (data == NULL) {
		return 0U;
	}
	size_t written = 0U;
	while (written < length && ringbuf_put(ring, data[written])) {
		written++;
	}
	return written;
}

size_t ringbuf_read(ringbuf_t *ring, uint8_t *data, size_t length)
{
	if (data == NULL) {
		return 0U;
	}
	size_t count = 0U;
	while (count < length && ringbuf_get(ring, &data[count])) {
		count++;
	}
	return count;
}

size_t ringbuf_count(const ringbuf_t *ring)
{
	if (!is_valid(ring)) {
		return 0U;
	}
	const size_t head = ring->head;
	const size_t tail = ring->tail;
	return (head >= tail) ? (head - tail) : (ring->size - tail + head);
}

size_t ringbuf_capacity(const ringbuf_t *ring)
{
	return is_valid(ring) ? (ring->size - 1U) : 0U;
}

size_t ringbuf_space(const ringbuf_t *ring)
{
	return ringbuf_capacity(ring) - ringbuf_count(ring);
}
