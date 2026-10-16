/*
 * Binary LED indicator for ZMK.
 *
 * Owns one GPIO outright - no PWM, no backlight subsystem, no settings
 * persistence. The LED is lit when either:
 *   - the indicated layer is active (e.g. the BT layer), or
 *   - the watched pointing device reported movement within the hold window.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>

LOG_MODULE_REGISTER(led_indicator, CONFIG_ZMK_LED_INDICATOR_LOG_LEVEL);

#define LED_NODE DT_NODELABEL(led_indicator)

#if !DT_NODE_EXISTS(LED_NODE)
#error "No node labelled led_indicator found in devicetree"
#endif

#define IND_LAYER        DT_PROP(LED_NODE, indicate_layer)
#define ACTIVITY_HOLD_MS DT_PROP(LED_NODE, activity_hold_ms)
#define BLINK_LAYER      DT_PROP(LED_NODE, blink_layer)
#define BLINK_MS         120

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

static bool layer_on;
static bool activity_on;
static bool hold_on;        /* e.g. a toggled mouse button is held */
static uint8_t blink_steps; /* remaining half-periods of a blink pattern */
static bool blink_lit;

static void led_apply(void) {
    bool lit = blink_steps ? blink_lit : (layer_on || activity_on || hold_on);
    gpio_pin_set_dt(&led, lit ? 1 : 0);
}

/* ---- blink patterns ---------------------------------------------------- */

static void blink_step_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(blink_work, blink_step_cb);

static void blink_step_cb(struct k_work *work) {
    ARG_UNUSED(work);
    if (blink_steps) {
        blink_steps--;
        blink_lit = !blink_lit;
    }
    led_apply();
    if (blink_steps) {
        k_work_reschedule(&blink_work, K_MSEC(BLINK_MS));
    }
}

/* n blinks: off-gap, then n x (on, off) */
static void led_blink(uint8_t n) {
    blink_lit = true;           /* first step turns it off */
    blink_steps = 1 + 2 * n;
    k_work_reschedule(&blink_work, K_NO_WAIT);
}

void led_indicator_set_hold(bool on) {
    hold_on = on;
    led_apply();
}

/* ---- pointer activity ------------------------------------------------- */

static void activity_off_cb(struct k_work *work) {
    ARG_UNUSED(work);
    activity_on = false;
    led_apply();
}

static K_WORK_DELAYABLE_DEFINE(activity_off_work, activity_off_cb);

#if DT_NODE_HAS_PROP(LED_NODE, activity_device)

static void led_input_cb(struct input_event *evt, void *user_data) {
    ARG_UNUSED(user_data);

    /* only relative motion counts as "the mouse moved" */
    if (evt->type != INPUT_EV_REL || evt->value == 0) {
        return;
    }

    if (!activity_on) {
        activity_on = true;
        led_apply();
    }
    /* sharp drop: reschedule the off-work on every report */
    k_work_reschedule(&activity_off_work, K_MSEC(ACTIVITY_HOLD_MS));
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_PHANDLE(LED_NODE, activity_device)), led_input_cb, NULL);

#endif /* activity_device */

/* ---- layer indication -------------------------------------------------- */

static int led_layer_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);

    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (ev->layer == IND_LAYER) {
        layer_on = ev->state;
        led_apply();
    }
    if (BLINK_LAYER != 255 && ev->layer == BLINK_LAYER) {
        led_blink(ev->state ? 2 : 1);   /* 2 blinks on entry, 1 on exit */
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(led_indicator, led_layer_listener);
ZMK_SUBSCRIPTION(led_indicator, zmk_layer_state_changed);

/* ---- init -------------------------------------------------------------- */

static int led_indicator_init(void) {
    if (!gpio_is_ready_dt(&led)) {
        LOG_ERR("LED GPIO port not ready");
        return -ENODEV;
    }

    int rc = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (rc < 0) {
        LOG_ERR("failed to configure LED GPIO: %d", rc);
        return rc;
    }

    LOG_INF("led_indicator ready: layer %d, activity hold %d ms", IND_LAYER, ACTIVITY_HOLD_MS);
    return 0;
}

SYS_INIT(led_indicator_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
