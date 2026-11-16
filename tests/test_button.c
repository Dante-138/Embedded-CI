/*
 * Host tests for src/button.c. Time is simulated: each call to button_update()
 * is one tick, so every timing rule is checked to the exact tick.
 */
#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "button.h"

static int failures;

#define CHECK(cond)                                                                                          \
	do {                                                                                                 \
		if (!(cond)) {                                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);             \
			failures++;                                                                          \
		}                                                                                            \
	} while (0)

enum { DEBOUNCE = 4, LONG_PRESS = 50, DOUBLE_GAP = 20 };

/* Deterministic pseudo-random generator (xorshift32): same sequence on every run. */
static uint32_t rng_state = 0x1357BEEFU;

static uint32_t random_u32(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

/* Events seen while feeding samples. */
typedef struct {
	unsigned clicks;
	unsigned doubles;
	unsigned longs;
	unsigned last_event_tick; /* 1-based index, within the last feed(), of the last event */
} seen_t;

static void count_event(seen_t *seen, button_event_t event, unsigned tick_number)
{
	switch (event) {
	case BUTTON_EVENT_CLICK:
		seen->clicks++;
		break;
	case BUTTON_EVENT_DOUBLE_CLICK:
		seen->doubles++;
		break;
	case BUTTON_EVENT_LONG_PRESS:
		seen->longs++;
		break;
	case BUTTON_EVENT_NONE:
	default:
		return;
	}
	seen->last_event_tick = tick_number;
}

/* Feeds the same level for `ticks` samples, adding the events to *seen. */
static void feed(button_t *button, bool level, unsigned ticks, seen_t *seen)
{
	seen->last_event_tick = 0U;
	for (unsigned i = 1U; i <= ticks; i++) {
		count_event(seen, button_update(button, level), i);
	}
}

static bool seen_is(const seen_t *seen, unsigned clicks, unsigned doubles, unsigned longs)
{
	return seen->clicks == clicks && seen->doubles == doubles && seen->longs == longs;
}

static button_t make_button(uint16_t long_press_ticks, uint16_t double_click_ticks)
{
	const button_config_t config = {
	    .debounce_ticks = DEBOUNCE,
	    .long_press_ticks = long_press_ticks,
	    .double_click_ticks = double_click_ticks,
	};
	button_t button;
	CHECK(button_init(&button, &config) == 0);
	return button;
}

static void test_init_rejects_bad_arguments(void)
{
	button_t button;
	button_config_t config = {.debounce_ticks = 0U, .long_press_ticks = 0U, .double_click_ticks = 0U};
	CHECK(button_init(&button, &config) == -EINVAL); /* debounce 0 would accept every glitch */
	config.debounce_ticks = 1U;
	CHECK(button_init(NULL, &config) == -EINVAL);
	CHECK(button_init(&button, NULL) == -EINVAL);
	CHECK(button_init(&button, &config) == 0);
	CHECK(!button_is_pressed(&button));
	CHECK(!button_is_pressed(NULL));
	CHECK(button_update(NULL, true) == BUTTON_EVENT_NONE);

	button_t zeroed;
	memset(&zeroed, 0, sizeof zeroed); /* a static button before button_init() */
	CHECK(button_update(&zeroed, true) == BUTTON_EVENT_NONE);
	CHECK(!button_is_pressed(&zeroed));
}

static void test_debounce_accepts_on_the_exact_tick(void)
{
	button_t button = make_button(0U, 0U);
	seen_t seen = {0};
	feed(&button, true, DEBOUNCE - 1U, &seen);
	CHECK(!button_is_pressed(&button)); /* one sample short */
	feed(&button, true, 1U, &seen);
	CHECK(button_is_pressed(&button));
	feed(&button, false, DEBOUNCE - 1U, &seen);
	CHECK(button_is_pressed(&button));
	CHECK(seen_is(&seen, 0U, 0U, 0U));
	feed(&button, false, 1U, &seen);
	CHECK(!button_is_pressed(&button));
	CHECK(seen_is(&seen, 1U, 0U, 0U)); /* click right at the debounced release */
}

static void test_glitches_shorter_than_debounce_are_ignored(void)
{
	button_t button = make_button(LONG_PRESS, DOUBLE_GAP);
	seen_t seen = {0};
	for (unsigned n = 0U; n < 5000U; n++) {
		feed(&button, true, 1U + (random_u32() % (DEBOUNCE - 1U)), &seen); /* 1 to DEBOUNCE-1 */
		feed(&button, false, 1U + (random_u32() % 10U), &seen);
		CHECK(!button_is_pressed(&button));
	}
	CHECK(seen_is(&seen, 0U, 0U, 0U));
}

static void test_bouncing_edges_give_one_click(void)
{
	button_t button = make_button(LONG_PRESS, 0U);
	seen_t seen = {0};
	/* Contact bounce on the way down, a solid press, bounce on the way up. */
	for (unsigned i = 0U; i < 6U; i++) {
		feed(&button, (i % 2U) == 0U, 1U, &seen);
	}
	feed(&button, true, 20U, &seen);
	for (unsigned i = 0U; i < 6U; i++) {
		feed(&button, (i % 2U) != 0U, 1U, &seen);
	}
	feed(&button, false, 20U, &seen);
	CHECK(seen_is(&seen, 1U, 0U, 0U));
}

static void test_glitch_while_held_does_not_release(void)
{
	button_t button = make_button(0U, 0U);
	seen_t seen = {0};
	feed(&button, true, 10U, &seen);
	feed(&button, false, DEBOUNCE - 1U, &seen); /* too short to be a release */
	feed(&button, true, 10U, &seen);
	CHECK(button_is_pressed(&button));
	CHECK(seen_is(&seen, 0U, 0U, 0U));
	feed(&button, false, DEBOUNCE, &seen);
	CHECK(seen_is(&seen, 1U, 0U, 0U));
}

static void test_long_press_fires_once_on_the_exact_tick(void)
{
	button_t button = make_button(LONG_PRESS, DOUBLE_GAP);
	seen_t seen = {0};
	feed(&button, true, DEBOUNCE, &seen); /* press accepted on this tick */
	feed(&button, true, LONG_PRESS - 1U, &seen);
	CHECK(seen_is(&seen, 0U, 0U, 0U)); /* one tick short */
	feed(&button, true, 1U, &seen);
	CHECK(seen_is(&seen, 0U, 0U, 1U));

	feed(&button, true, 500U, &seen);  /* still held: no repeat */
	feed(&button, false, 200U, &seen); /* release: no click after a long press */
	CHECK(seen_is(&seen, 0U, 0U, 1U));

	feed(&button, true, DEBOUNCE + 2U, &seen); /* the next short press is a normal click */
	feed(&button, false, DEBOUNCE + DOUBLE_GAP, &seen);
	CHECK(seen_is(&seen, 1U, 0U, 1U));
}

static void test_long_press_disabled_gives_click(void)
{
	button_t button = make_button(0U, 0U);
	seen_t seen = {0};
	/* Longer than the 16-bit timer can count: it must saturate, not wrap. */
	feed(&button, true, 70000U, &seen);
	CHECK(seen_is(&seen, 0U, 0U, 0U));
	feed(&button, false, DEBOUNCE, &seen);
	CHECK(seen_is(&seen, 1U, 0U, 0U));
}

static void test_long_press_survives_timer_saturation(void)
{
	const button_config_t config = {
	    .debounce_ticks = 1U, .long_press_ticks = UINT16_MAX, .double_click_ticks = 0U};
	button_t button;
	CHECK(button_init(&button, &config) == 0);
	seen_t seen = {0};
	feed(&button, true, 1U, &seen);
	feed(&button, true, (unsigned)UINT16_MAX - 1U, &seen);
	CHECK(seen_is(&seen, 0U, 0U, 0U));
	feed(&button, true, 1U, &seen);
	CHECK(seen_is(&seen, 0U, 0U, 1U));
	feed(&button, true, 70000U, &seen);
	CHECK(seen_is(&seen, 0U, 0U, 1U));
}

static void test_single_click_waits_for_the_double_gap(void)
{
	button_t button = make_button(LONG_PRESS, DOUBLE_GAP);
	seen_t seen = {0};
	feed(&button, true, DEBOUNCE + 3U, &seen);
	feed(&button, false, DEBOUNCE, &seen); /* release accepted on this tick */
	feed(&button, false, DOUBLE_GAP - 1U, &seen);
	CHECK(seen_is(&seen, 0U, 0U, 0U)); /* still waiting for a possible second press */
	feed(&button, false, 1U, &seen);
	CHECK(seen_is(&seen, 1U, 0U, 0U));
	feed(&button, false, 200U, &seen);
	CHECK(seen_is(&seen, 1U, 0U, 0U));
}

static void test_double_click(void)
{
	button_t button = make_button(LONG_PRESS, DOUBLE_GAP);
	seen_t seen = {0};
	feed(&button, true, DEBOUNCE + 3U, &seen);
	feed(&button, false, DEBOUNCE + 5U, &seen);
	feed(&button, true, DEBOUNCE + 3U, &seen);
	CHECK(seen_is(&seen, 0U, 0U, 0U));
	feed(&button, false, DEBOUNCE, &seen);
	CHECK(seen_is(&seen, 0U, 1U, 0U));
	CHECK(seen.last_event_tick == DEBOUNCE); /* on the debounced second release */
	feed(&button, false, 200U, &seen);
	CHECK(seen_is(&seen, 0U, 1U, 0U)); /* and no extra single click */
}

static void test_second_press_on_the_last_tick_of_the_gap(void)
{
	/*
	 * The gap is measured on the debounced level: from the accepted release to
	 * the accepted second press. Below, the second press is accepted on tick
	 * DOUBLE_GAP after the release, the last one that still makes a double click.
	 */
	button_t button = make_button(0U, DOUBLE_GAP);
	seen_t seen = {0};
	feed(&button, true, DEBOUNCE, &seen);
	feed(&button, false, DEBOUNCE, &seen);
	feed(&button, false, DOUBLE_GAP - DEBOUNCE, &seen);
	feed(&button, true, DEBOUNCE, &seen);
	feed(&button, false, DEBOUNCE, &seen);
	CHECK(seen_is(&seen, 0U, 1U, 0U));

	/* One tick later it is too late: two single clicks. */
	button = make_button(0U, DOUBLE_GAP);
	memset(&seen, 0, sizeof seen);
	feed(&button, true, DEBOUNCE, &seen);
	feed(&button, false, DEBOUNCE, &seen);
	feed(&button, false, DOUBLE_GAP - DEBOUNCE + 1U, &seen);
	feed(&button, true, DEBOUNCE, &seen);
	CHECK(seen_is(&seen, 1U, 0U, 0U));
	feed(&button, false, DEBOUNCE + DOUBLE_GAP, &seen);
	CHECK(seen_is(&seen, 2U, 0U, 0U));
}

static void test_corrupted_state_restarts(void)
{
	button_t button = make_button(LONG_PRESS, 0U);
	memset(&button.state, 0x7F, sizeof button.state); /* not a valid state */
	seen_t seen = {0};
	feed(&button, false, 1U, &seen);
	feed(&button, true, DEBOUNCE + 2U, &seen);
	feed(&button, false, DEBOUNCE, &seen);
	CHECK(seen_is(&seen, 1U, 0U, 0U));
}

/*
 * Random presses of random length with clean edges, double click disabled:
 * every press gives exactly one event, a click if it was shorter than the long
 * press time and a long press otherwise.
 */
static void test_random_presses_one_event_each(void)
{
	button_t button = make_button(LONG_PRESS, 0U);
	for (unsigned n = 0U; n < 5000U; n++) {
		const unsigned held = DEBOUNCE + (random_u32() % (3U * LONG_PRESS));
		seen_t seen = {0};
		feed(&button, true, held, &seen);
		feed(&button, false, DEBOUNCE + (random_u32() % 10U), &seen);
		/*
		 * The debounced level lags the pin by DEBOUNCE ticks on the press and by
		 * DEBOUNCE - 1 counted ticks on the release, so it stays pressed for
		 * held - 1 ticks after the press is accepted.
		 */
		if (held - 1U >= LONG_PRESS) {
			CHECK(seen_is(&seen, 0U, 0U, 1U));
		} else {
			CHECK(seen_is(&seen, 1U, 0U, 0U));
		}
	}
}

/* Pure random levels: nothing to assert on events, only that no state breaks. */
static void test_random_noise_keeps_state_valid(void)
{
	button_t button = make_button(LONG_PRESS, DOUBLE_GAP);
	for (unsigned n = 0U; n < 200000U; n++) {
		const button_event_t event = button_update(&button, (random_u32() & 3U) != 0U);
		CHECK(event == BUTTON_EVENT_NONE || event == BUTTON_EVENT_CLICK ||
		      event == BUTTON_EVENT_DOUBLE_CLICK || event == BUTTON_EVENT_LONG_PRESS);
		CHECK(button.state <= BUTTON_STATE_SECOND_PRESSED);
		CHECK(button.counter < DEBOUNCE);
	}
}

int main(void)
{
	test_init_rejects_bad_arguments();
	test_debounce_accepts_on_the_exact_tick();
	test_glitches_shorter_than_debounce_are_ignored();
	test_bouncing_edges_give_one_click();
	test_glitch_while_held_does_not_release();
	test_long_press_fires_once_on_the_exact_tick();
	test_long_press_disabled_gives_click();
	test_long_press_survives_timer_saturation();
	test_single_click_waits_for_the_double_gap();
	test_double_click();
	test_second_press_on_the_last_tick_of_the_gap();
	test_corrupted_state_restarts();
	test_random_presses_one_event_each();
	test_random_noise_keeps_state_valid();

	if (failures != 0) {
		fprintf(stderr, "button: %d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	printf("button: all tests passed\n");
	return EXIT_SUCCESS;
}
