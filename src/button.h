/*
 * button: debounce plus a small state machine that turns a raw GPIO level into
 * click, double click and long press events.
 *
 * Call button_update() at a fixed period (the "tick", typically 5 to 10 ms)
 * from a timer or the main loop, passing the raw level read from the pin. All
 * times are in ticks, so the module has no clock and no hardware dependency.
 *
 * Debounce: the level only changes after `debounce_ticks` consecutive samples
 * that differ from the current stable level. Any shorter pulse is ignored, on
 * press and on release. All the times below are measured on this debounced
 * level, which lags the pin by debounce_ticks on both edges.
 *
 * State machine (on the debounced level):
 *
 *   IDLE --press--> PRESSED --release--> WAIT_SECOND --press--> SECOND_PRESSED
 *                      |                      |                       |
 *                      | held long_press      | double_click_ticks    | release
 *                      v                      v                       v
 *                 LONG_PRESS event        CLICK event          DOUBLE_CLICK event
 *                 (HELD until release)
 *
 * - With double_click_ticks = 0 the CLICK comes right at the release.
 *   Otherwise it comes double_click_ticks later, because only then is it known
 *   that no second press followed. That delay is the price of double click.
 * - With long_press_ticks = 0 there is no long press: any hold is a click.
 * - A long press fires once, while the button is still held, and the release
 *   that follows gives no event.
 * - Holding the second press of a double click is still a double click.
 *
 * No dynamic allocation: one button_t per button, owned by the caller.
 */
#ifndef BUTTON_H
#define BUTTON_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_CLICK,
    BUTTON_EVENT_DOUBLE_CLICK,
    BUTTON_EVENT_LONG_PRESS
} button_event_t;

typedef struct {
    uint16_t debounce_ticks;     /* >= 1: consecutive samples needed to accept a level */
    uint16_t long_press_ticks;   /* hold time for LONG_PRESS; 0 disables it */
    uint16_t double_click_ticks; /* max gap between two clicks; 0 disables double click */
} button_config_t;

/* Internal states. Public only so the button can be allocated by the caller. */
typedef enum {
    BUTTON_STATE_IDLE = 0,
    BUTTON_STATE_PRESSED,
    BUTTON_STATE_HELD,
    BUTTON_STATE_WAIT_SECOND,
    BUTTON_STATE_SECOND_PRESSED
} button_state_t;

/* Treat the fields as private: use the functions below. */
typedef struct {
    button_config_t config;
    button_state_t state;
    bool pressed;     /* debounced level */
    uint16_t counter; /* consecutive samples that differ from `pressed` */
    uint16_t timer;   /* ticks spent in the current timed state */
} button_t;

/*
 * Starts in the released state. Returns false if an argument is NULL or
 * debounce_ticks is 0. The config is copied.
 */
bool button_init(button_t *button, const button_config_t *config);

/*
 * Feeds one sample (true = pressed) and returns the event it completed.
 * Call once per tick. Returns BUTTON_EVENT_NONE for a NULL button.
 */
button_event_t button_update(button_t *button, bool raw_pressed);

/* Debounced level: true while the button is accepted as pressed. */
bool button_is_pressed(const button_t *button);

#endif /* BUTTON_H */
