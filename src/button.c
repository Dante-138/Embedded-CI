#include "button.h"

#include <stddef.h>

typedef enum { EDGE_NONE = 0, EDGE_PRESS, EDGE_RELEASE } edge_t;

/* Counts up without wrapping: a button held for days must not restart a timer. */
static void tick(uint16_t *timer)
{
    if (*timer < UINT16_MAX) {
        (*timer)++;
    }
}

/* Debounce: accepts a new level only after debounce_ticks samples in a row. */
static edge_t debounce(button_t *button, bool raw_pressed)
{
    if (raw_pressed == button->pressed) {
        button->counter = 0U; /* any sample at the stable level restarts the count */
        return EDGE_NONE;
    }
    button->counter++;
    if (button->counter < button->config.debounce_ticks) {
        return EDGE_NONE;
    }
    button->counter = 0U;
    button->pressed = raw_pressed;
    return raw_pressed ? EDGE_PRESS : EDGE_RELEASE;
}

bool button_init(button_t *button, const button_config_t *config)
{
    if (button == NULL || config == NULL || config->debounce_ticks == 0U) {
        return false;
    }
    button->config = *config;
    button->state = BUTTON_STATE_IDLE;
    button->pressed = false;
    button->counter = 0U;
    button->timer = 0U;
    return true;
}

button_event_t button_update(button_t *button, bool raw_pressed)
{
    if (button == NULL || button->config.debounce_ticks == 0U) {
        return BUTTON_EVENT_NONE; /* NULL or never initialized */
    }
    const edge_t edge = debounce(button, raw_pressed);

    switch (button->state) {
    case BUTTON_STATE_IDLE:
        if (edge == EDGE_PRESS) {
            button->state = BUTTON_STATE_PRESSED;
            button->timer = 0U;
        }
        return BUTTON_EVENT_NONE;

    case BUTTON_STATE_PRESSED:
        if (edge == EDGE_RELEASE) {
            if (button->config.double_click_ticks == 0U) {
                button->state = BUTTON_STATE_IDLE;
                return BUTTON_EVENT_CLICK;
            }
            button->state = BUTTON_STATE_WAIT_SECOND;
            button->timer = 0U;
            return BUTTON_EVENT_NONE;
        }
        tick(&button->timer);
        if (button->config.long_press_ticks != 0U && button->timer >= button->config.long_press_ticks) {
            button->state = BUTTON_STATE_HELD;
            return BUTTON_EVENT_LONG_PRESS;
        }
        return BUTTON_EVENT_NONE;

    case BUTTON_STATE_HELD:
        if (edge == EDGE_RELEASE) {
            button->state = BUTTON_STATE_IDLE; /* the long press was already reported */
        }
        return BUTTON_EVENT_NONE;

    case BUTTON_STATE_WAIT_SECOND:
        if (edge == EDGE_PRESS) {
            button->state = BUTTON_STATE_SECOND_PRESSED;
            return BUTTON_EVENT_NONE;
        }
        tick(&button->timer);
        if (button->timer >= button->config.double_click_ticks) {
            button->state = BUTTON_STATE_IDLE;
            return BUTTON_EVENT_CLICK; /* no second press came: it was a single click */
        }
        return BUTTON_EVENT_NONE;

    case BUTTON_STATE_SECOND_PRESSED:
        if (edge == EDGE_RELEASE) {
            button->state = BUTTON_STATE_IDLE;
            return BUTTON_EVENT_DOUBLE_CLICK;
        }
        return BUTTON_EVENT_NONE;

    default:
        /* Corrupted state (e.g. stray write into the struct): start over. */
        button->state = BUTTON_STATE_IDLE;
        button->timer = 0U;
        return BUTTON_EVENT_NONE;
    }
}

bool button_is_pressed(const button_t *button)
{
    return (button != NULL) && button->pressed;
}
