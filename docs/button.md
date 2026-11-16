# button

Debounce plus a state machine that turns a raw GPIO level into click, double
click and long press: [`src/button.h`](../src/button.h),
[`src/button.c`](../src/button.c).

## Problem it solves

A button that fires twice on one press (contact bounce), and click, double
click and long press logic spread over `if`s and timestamps in the main loop.

## Behavior

- Call `button_update()` once per tick (typically every 5 to 10 ms) with the
  level read from the pin. All times are in ticks: no clock, no hardware.
- Debounce: a level is accepted only after `debounce_ticks` samples in a row.
  Shorter pulses are ignored, on press and on release.
- Times are measured on the debounced level, which lags the pin by
  `debounce_ticks` on both edges.
- States: `IDLE`, `PRESSED`, `HELD`, `WAIT_SECOND`, `SECOND_PRESSED`.

| Event | When it is returned |
| --- | --- |
| `BUTTON_EVENT_CLICK` | at the release if `double_click_ticks` is 0; otherwise `double_click_ticks` after the release, once no second press came |
| `BUTTON_EVENT_DOUBLE_CLICK` | at the release of a second press that started within `double_click_ticks` |
| `BUTTON_EVENT_LONG_PRESS` | once, while still held, after `long_press_ticks`; the release that follows gives no event |

- `long_press_ticks = 0` disables long press; `double_click_ticks = 0` disables
  double click and removes the delay on the single click.
- The button starts as released. If it is held at power-up, the press is
  accepted after `debounce_ticks` like any other, and goes on to a click or a
  long press. An application that must ignore a button held at boot should
  discard events until it has seen the button released once.

## Usage

```c
#include "button.h"

static button_t user_button;

void buttons_init(void)
{
	/* With a 10 ms tick: 30 ms debounce, 1 s long press, 300 ms double click gap. */
	const button_config_t config = {
		.debounce_ticks = 3,
		.long_press_ticks = 100,
		.double_click_ticks = 30,
	};
	(void)button_init(&user_button, &config);
}

void on_10ms_tick(void)
{
	const bool pressed = (gpio_read(BUTTON_PIN) == 0); /* active low */
	switch (button_update(&user_button, pressed)) {
	case BUTTON_EVENT_CLICK:
		toggle_led();
		break;
	case BUTTON_EVENT_DOUBLE_CLICK:
		next_mode();
		break;
	case BUTTON_EVENT_LONG_PRESS:
		start_pairing();
		break;
	default:
		break;
	}
}
```

## Tests ([tests/test_button.c](../tests/test_button.c))

Run with ASan + UBSan. Time is simulated (one call is one tick), so the timing
rules are checked to the exact tick.

- `button_init()` returns `-EINVAL` for `NULL` and `debounce_ticks = 0`; `NULL` and a zeroed
  button are safe.
- Debounce accepts press and release on the exact tick, one sample short does
  nothing.
- 5,000 random glitches shorter than the debounce time: no event, never
  pressed.
- Bouncing edges around a solid press give exactly one click; a glitch while
  held does not release.
- Long press fires once on the exact tick, does not repeat, and the release
  gives no click.
- A hold of 70,000 ticks with long press disabled is still one click, and a
  long press at the largest setting (65,535) still fires once: the 16-bit timer
  saturates instead of wrapping.
- Single click is delayed by exactly the double click gap; double click gives
  one event and no extra click; the last tick that still counts as double, and
  one tick later (two clicks).
- A corrupted state restarts cleanly.
- 5,000 random presses with clean edges: exactly one event each, click or long
  press according to the hold time.
- 200,000 random samples: the state and the counters stay in range.

Not covered: real switch bounce on hardware. Choose `debounce_ticks` from the
bounce time of the switch (commonly 5 to 20 ms) and the tick period.

## Copy into a project

Copy `src/button.c` and `src/button.h`. Use one `button_t` per button.
